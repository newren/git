/*
 * "git replay" builtin command
 */

#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"

#include "builtin.h"
#include "advice.h"
#include "commit-reach.h"
#include "environment.h"
#include "hex.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "object-name.h"
#include "parse-options.h"
#include "path.h"
#include "refs.h"
#include "revision.h"
#include "strmap.h"
#include <oidset.h>
#include <tree.h>

#define the_repository DO_NOT_USE_THE_REPOSITORY

static struct commit *peel_committish(struct repository *repo, const char *name)
{
	struct object *obj;
	struct object_id oid;

	if (repo_get_oid(repo, name, &oid))
		return NULL;
	obj = parse_object(repo, &oid);
	return (struct commit *)repo_peel_to_type(repo, name, 0, obj,
						  OBJ_COMMIT);
}

static char *get_author(const char *message)
{
	size_t len;
	const char *a;

	a = find_commit_header(message, "author", &len);
	if (a)
		return xmemdupz(a, len);

	return NULL;
}

static struct commit *create_commit(struct repository *repo,
				    struct tree *tree,
				    struct commit *based_on,
				    ... /* a bunch of struct commit * parent */)
{
	va_list parent_list;
	struct commit *parent;
	struct object_id ret;
	struct object *obj = NULL;
	struct commit_list *parents = NULL, **tmp = &parents;
	char *author;
	char *sign_commit = NULL; /* FIXME: cli users might want to sign again */
	struct commit_extra_header *extra = NULL;
	struct strbuf msg = STRBUF_INIT;
	const char *out_enc = get_commit_output_encoding();
	const char *message = repo_logmsg_reencode(repo, based_on,
						   NULL, out_enc);
	const char *orig_message = NULL;
	const char *exclude_gpgsig[] = { "gpgsig", NULL };

	va_start(parent_list, based_on);
	while ((parent = va_arg(parent_list, struct commit *)))
		tmp = commit_list_append(parent, tmp);
	va_end(parent_list);

	extra = read_commit_extra_headers(based_on, exclude_gpgsig);
	find_commit_subject(message, &orig_message);
	strbuf_addstr(&msg, orig_message);
	author = get_author(message);
	reset_ident_date();
	if (commit_tree_extended(msg.buf, msg.len, &tree->object.oid, parents,
				 &ret, author, NULL, sign_commit, extra)) {
		error(_("failed to write commit object"));
		goto out;
	}

	obj = parse_object(repo, &ret);

out:
	repo_unuse_commit_buffer(repo, based_on, message);
	free_commit_extra_headers(extra);
	free_commit_list(parents);
	strbuf_release(&msg);
	free(author);
	return (struct commit *)obj;
}

struct ref_info {
	struct commit *onto;
	struct strset positive_refs;
	struct strset negative_refs;
	int positive_refexprs;
	int negative_refexprs;
};

static void record_remapping(kh_oid_map_t *replayed_commits,
			     struct object_id *oid,
			     struct commit *maps_to)
{
	int hr;
	khint_t pos = kh_put_oid_map(replayed_commits, *oid, &hr);
	if (hr == 0)
		BUG("Duplicate rewritten commit: %s\n", oid_to_hex(oid));
	kh_value(replayed_commits, pos) = maps_to;
}

static void get_ref_information(struct repository *repo,
				struct rev_cmdline_info *cmd_info,
				struct ref_info *ref_info,
				kh_oid_map_t *replayed_commits,
				struct commit *newbase)
{
	int i;

	ref_info->onto = NULL;
	strset_init(&ref_info->positive_refs);
	strset_init(&ref_info->negative_refs);
	ref_info->positive_refexprs = 0;
	ref_info->negative_refexprs = 0;

	/*
	 * When the user specifies e.g.
	 *   git replay origin/main..mybranch
	 *   git replay ^origin/next mybranch1 mybranch2
	 * we want to be able to determine where to replay the commits.  In
	 * these examples, the branches are probably based on an old version
	 * of either origin/main or origin/next, so we want to replay on the
	 * newest version of that branch.  In contrast we would want to error
	 * out if they ran
	 *   git replay ^origin/master ^origin/next mybranch
	 *   git replay mybranch~2..mybranch
	 * the first of those because there's no unique base to choose, and
	 * the second because they'd likely just be replaying commits on top
	 * of the same commit and not making any difference.
	 */
	for (i = 0; i < cmd_info->nr; i++) {
		struct rev_cmdline_entry *e = cmd_info->rev + i;
		struct object_id oid;
		const char *refexpr = e->name;
		char *fullname = NULL;
		int can_uniquely_dwim = 1;

		if (*refexpr == '^')
			refexpr++;
		if (repo_dwim_ref(repo, refexpr, strlen(refexpr), &oid, &fullname, 0) != 1)
			can_uniquely_dwim = 0;

		if (e->flags & BOTTOM) {
			if (can_uniquely_dwim)
				strset_add(&ref_info->negative_refs, fullname);
			if (!ref_info->negative_refexprs)
				ref_info->onto = lookup_commit_reference_gently(repo,
										&e->item->oid, 1);
			ref_info->negative_refexprs++;

			if (newbase)
				record_remapping(replayed_commits,
						 &e->item->oid, newbase);
		} else {
			if (can_uniquely_dwim)
				strset_add(&ref_info->positive_refs, fullname);
			ref_info->positive_refexprs++;
		}

		free(fullname);
	}
}

static void determine_replay_mode(struct repository *repo,
				  struct rev_cmdline_info *cmd_info,
				  kh_oid_map_t *replayed_commits,
				  const char *onto_name,
				  char **advance_name,
				  struct commit **onto,
				  struct strset **update_refs)
{
	struct ref_info rinfo;

	if (onto_name) {
		*onto = peel_committish(repo, onto_name);
	} else if (*advance_name) {
		*onto = peel_committish(repo, *advance_name);
	} else {
		*onto = NULL;
	}
	get_ref_information(repo, cmd_info, &rinfo, replayed_commits, *onto);

	if (!rinfo.positive_refexprs)
		die(_("need some commits to replay"));

	die_for_incompatible_opt2(!!onto_name, "--onto",
				  !!*advance_name, "--advance");
	if (onto_name) {
		*onto = peel_committish(repo, onto_name);
		if (rinfo.positive_refexprs <
		    strset_get_size(&rinfo.positive_refs))
			die(_("all positive revisions given must be references"));
	} else if (*advance_name) {
		struct object_id oid;
		char *fullname = NULL;

		*onto = peel_committish(repo, *advance_name);
		if (repo_dwim_ref(repo, *advance_name, strlen(*advance_name),
			     &oid, &fullname, 0) == 1) {
			free(*advance_name);
			*advance_name = fullname;
		} else {
			die(_("argument to --advance must be a reference"));
		}
		if (rinfo.positive_refexprs > 1)
			die(_("cannot advance target with multiple sources because ordering would be ill-defined"));
	} else {
		int positive_refs_complete = (
			rinfo.positive_refexprs ==
			strset_get_size(&rinfo.positive_refs));
		int negative_refs_complete = (
			rinfo.negative_refexprs ==
			strset_get_size(&rinfo.negative_refs));
		/*
		 * We need either positive_refs_complete or
		 * negative_refs_complete, but not both.
		 */
		if (rinfo.negative_refexprs > 0 &&
		    positive_refs_complete == negative_refs_complete)
			die(_("cannot implicitly determine whether this is an --advance or --onto operation"));
		if (negative_refs_complete) {
			struct hashmap_iter iter;
			struct strmap_entry *entry;
			const char *last_key = NULL;

			if (rinfo.negative_refexprs == 0)
				die(_("all positive revisions given must be references"));
			else if (rinfo.negative_refexprs > 1)
				die(_("cannot implicitly determine whether this is an --advance or --onto operation"));
			else if (rinfo.positive_refexprs > 1)
				die(_("cannot advance target with multiple source branches because ordering would be ill-defined"));

			/* Only one entry, but we have to loop to get it */
			strset_for_each_entry(&rinfo.negative_refs,
					      &iter, entry) {
				last_key = entry->key;
			}

			free(*advance_name);
			*advance_name = xstrdup_or_null(last_key);
		} else { /* positive_refs_complete */
			if (rinfo.negative_refexprs > 1)
				die(_("cannot implicitly determine correct base for --onto"));
			if (rinfo.negative_refexprs == 1)
				*onto = rinfo.onto;
		}
	}
	if (!*advance_name) {
		*update_refs = xcalloc(1, sizeof(**update_refs));
		**update_refs = rinfo.positive_refs;
		memset(&rinfo.positive_refs, 0, sizeof(**update_refs));
	}
	strset_clear(&rinfo.negative_refs);
	strset_clear(&rinfo.positive_refs);
}

static struct commit *mapped_commit(kh_oid_map_t *replayed_commits,
				    struct commit *commit,
				    struct commit *fallback)
{
	khint_t pos = kh_get_oid_map(replayed_commits, commit->object.oid);
	if (pos != kh_end(replayed_commits))
		return kh_value(replayed_commits, pos);
	if (commit->object.flags & UNINTERESTING)
		return fallback;
	return commit;
}

static struct commit *pick_regular_commit(struct repository *repo,
					  struct commit *pickme,
					  kh_oid_map_t *replayed_commits,
					  struct commit *onto,
					  struct merge_options *merge_opt,
					  struct merge_result *result)
{
	struct commit *base, *replayed_base;
	struct tree *pickme_tree, *base_tree;
	struct pretty_print_context ctx = {0};
	struct strbuf parent1_desc = STRBUF_INIT;
	struct strbuf parent2_desc = STRBUF_INIT;

	base = pickme->parents->item;
	replayed_base = mapped_commit(replayed_commits, base, onto);

	result->tree = repo_get_commit_tree(repo, replayed_base);
	pickme_tree = repo_get_commit_tree(repo, pickme);
	base_tree = repo_get_commit_tree(repo, base);

	ctx.abbrev = DEFAULT_ABBREV;
	repo_format_commit_message(repo, replayed_base, "%h (%s)",
				   &parent1_desc, &ctx);
	repo_format_commit_message(repo, pickme,        "%h (%s)",
				   &parent2_desc, &ctx);
	merge_opt->branch1 = parent1_desc.buf;
	merge_opt->branch2 = parent2_desc.buf;
	merge_opt->ancestor = xstrfmt("parent of %s", merge_opt->branch2);

	merge_incore_nonrecursive(merge_opt,
				  base_tree,
				  result->tree,
				  pickme_tree,
				  result);

	free((char*)merge_opt->ancestor);
	merge_opt->ancestor = NULL;
	strbuf_release(&parent1_desc);
	strbuf_release(&parent2_desc);
	if (!result->clean)
		return NULL;

	/*
	 * If our new commit would be empty, i.e. its tree matches its parent,
	 * and the original commit we are picking is not empty, i.e. its tree
	 * does not match its parent, then do not create the empty commit and
	 * just return our parent.
	 */
	if (oideq(&result->tree->object.oid,
		  &repo_get_commit_tree(repo, replayed_base)->object.oid) &&
	    !oideq(&repo_get_commit_tree(repo, pickme)->object.oid,
		  &repo_get_commit_tree(repo, pickme->parents->item)->object.oid))
		return replayed_base;
	/* ...otherwise, create the commit. */
	return create_commit(repo, result->tree, pickme, replayed_base, NULL);
}

static void do_merge(struct repository *repo,
		     struct commit *parent1,
		     struct commit *parent2,
		     struct merge_options *o,
		     struct merge_result *result)
{
	/* Caller must call merge_finalize */
	struct commit_list *bases = NULL;

	/* Setup merge options */
	init_basic_merge_options(o, repo);
	o->show_rename_progress = 0;
	o->record_conflict_msgs_as_headers = 1;
	o->msg_header_prefix = "remerge";

	o->ancestor = "ancestor"; /* TODO */
	o->branch1 = "parent1"; /* TODO */
	o->branch2 = "parent2";

	/* Parse the relevant commits and get the merge bases */
	//parse_commit_or_die(parent1);
	//parse_commit_or_die(parent2);
	if (repo_get_merge_bases(repo, parent1, parent2, &bases) < 0) {
		die(_("failed to find merge bases of %s and %s"),
		    oid_to_hex(&parent1->object.oid),
		    oid_to_hex(&parent2->object.oid));
	}

	/* Re-merge the parents */
	merge_incore_recursive(o, bases, parent1, parent2, result);
}

static struct commit *pick_merge_commit(struct repository *repo,
					struct commit *pickme,
					kh_oid_map_t *replayed_commits,
					struct commit *onto UNUSED,
					struct merge_options *merge_opt,
					struct merge_result *result)
{
	struct commit *parent1, *parent2, *replayed_par1, *replayed_par2;
	struct tree *remerge_tree, *pickme_tree, *new_merge_tree;
	struct pretty_print_context ctx = {0};
	struct strbuf remerge_desc = STRBUF_INIT;
	struct strbuf pickme_desc = STRBUF_INIT;
	struct strbuf new_merge_desc = STRBUF_INIT;
	struct merge_options remerge_opt = { 0 }, new_merge_opt = { 0 };
	struct merge_result remerge_res = { 0 }, new_merge_res = { 0 };

	parent1 = pickme->parents->item;
	replayed_par1 = mapped_commit(replayed_commits, parent1, parent1);
	parent2 = pickme->parents->next->item;
	replayed_par2 = mapped_commit(replayed_commits, parent2, parent2);

	/*
	 * We need the trees from 3 merges:
	 *   1. remerge of pickme (may have conflicts, but get the tree)
	 *   2. pickme (already have it above as pickme_tree)
	 *   3. merge of replayed_par[12] (may also have conflicts)
	 *
	 * Once we have these 3 merge commits, we take their trees and do
	 * a non-recursive merges on those, treating #2 as the base.  The
	 * result of "merge of merges" is our picked commit.
	 */

	do_merge(repo, parent1, parent2, &remerge_opt, &remerge_res);
	do_merge(repo, replayed_par1, replayed_par2, &new_merge_opt, &new_merge_res);

	remerge_tree = remerge_res.tree;
	pickme_tree = repo_get_commit_tree(repo, pickme);
	new_merge_tree = new_merge_res.tree;

	ctx.abbrev = DEFAULT_ABBREV;
	repo_format_commit_message(repo, pickme, "remerge of %h (%s)",
				   &remerge_desc, &ctx);
	repo_format_commit_message(repo, pickme, "%h (%s)",
				   &pickme_desc, &ctx);
	repo_format_commit_message(repo, pickme, "new merge of %h (%s)",
				   &new_merge_desc, &ctx);
	merge_opt->ancestor = remerge_desc.buf;
	merge_opt->branch1 = pickme_desc.buf;
	merge_opt->branch2 = new_merge_desc.buf;

	merge_incore_nonrecursive(merge_opt,
				  remerge_tree,
				  pickme_tree,
				  new_merge_tree,
				  result);

	free((char*)merge_opt->ancestor);
	merge_opt->ancestor = NULL;
	free((char*)merge_opt->branch1);
	free((char*)merge_opt->branch2);
	/*
	 * TODO: relax this; intermediate results can have conflicts
	 * without outer one having some
	 */
	if (result->clean != 1)
		return NULL;

	return create_commit(repo, result->tree, pickme,
			     replayed_par1, replayed_par2, NULL);
}

static struct commit *pick_octopus_commit(struct repository *repo UNUSED,
					  struct commit *pickme UNUSED,
					  kh_oid_map_t *replayed_commits UNUSED,
					  struct commit *onto UNUSED,
					  struct merge_options *merge_opt UNUSED,
					  struct merge_result *result UNUSED)
{
	/* Remember: do not capitalize first letter of the error message! */
	BUG("nOT IMPLEMENTED!!!");
}

static int add_ref_to_transaction(struct ref_transaction *transaction,
				  const char *refname,
				  const struct object_id *new_oid,
				  const struct object_id *old_oid,
				  struct strbuf *err)
{
	return ref_transaction_update(transaction, refname, new_oid, old_oid,
				      NULL, NULL, 0, "replay finished", err);
}

static int edit(int argc, const char **argv, const char *prefix,
		struct repository *repo)
{
	struct string_list refs = STRING_LIST_INIT_NODUP;
	const char * const builtin_replay_edit_usage[] = {
		N_("git replay edit [<options>] <commit>"),
		NULL
	};
	struct option options[] = {
		OPT_STRING_LIST('r', "reference", &refs, N_("reference"),
				N_("reference(s) to update")),
		OPT_END()
	};
	int fd;
	struct strbuf sb = STRBUF_INIT;

	/* FIXME: Verify that we're in a non-bare repository */

	argc = parse_options(argc, argv, prefix, options,
			     builtin_replay_edit_usage, 0);

	if (argc >= 4)
		usage_with_options(builtin_replay_edit_usage, options);

	if (refs.nr)
		die(_("handling user-specified references not yet implemented"));
	else
		string_list_append(&refs, "--branches");

	if (argc == 2) {
		const char *new_argv[4] = { "switch", "--detach", "--quiet",
					    argv[1] };
		cmd_switch(4, new_argv, prefix, repo);
		advise_if_enabled(ADVICE_REPLAY_EDIT,
			_("Switched your working tree to %s.\n"
			  "Started editing mode: any edits will cause descendant commits \n"
			  "to be replayed on top.\n"
			  "Use \"git switch <branch>\" (or git checkout) to exit editing mode.\n"),
			argv[1]);
	} else {
		die(_("auto-picking conflicted commits to edit not yet implemented"));
	}
	fd = xopen(git_path_replay_edit(repo), O_CREAT | O_WRONLY, 0666);
	strbuf_add_separated_string_list(&sb, "\n", &refs);
	strbuf_complete_line(&sb);
	write_in_full(fd, sb.buf, sb.len);
	close(fd);
	strbuf_release(&sb);

	return 0;
}

int cmd_replay(int argc,
	       const char **argv,
	       const char *prefix,
	       struct repository *repo)
{
	const char *advance_name_opt = NULL;
	char *advance_name = NULL;
	struct commit *onto = NULL;
	const char *onto_name = NULL;
	int contained = 0;
	int no_update_refs_flag = 0;
	int brief_stats = 0;
	int batch_mode = 0;
	int commits_updated = 0;
	int branches_updated = 0;

	struct rev_info revs;
	struct commit *last_commit = NULL;
	struct commit *commit;
	struct merge_options merge_opt;
	struct merge_result result;
	struct strset *update_refs = NULL;
	kh_oid_map_t *replayed_commits;
	struct ref_transaction *transaction = NULL;
	struct strbuf transaction_err = STRBUF_INIT;
	int ret = 0;

	const char * const replay_usage[] = {
		N_("(EXPERIMENTAL!) git replay "
		   "([--contained] --onto <newbase> | --advance <branch>) "
		   "[--no-update-refs] <revision-range>..."),
		NULL
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option replay_options[] = {
		OPT_SUBCOMMAND("edit", &fn, edit),
		OPT_STRING(0, "advance", &advance_name_opt,
			   N_("branch"),
			   N_("make replay advance given branch")),
		OPT_STRING(0, "onto", &onto_name,
			   N_("revision"),
			   N_("replay onto given commit")),
		OPT_BOOL(0, "contained", &contained,
			 N_("advance all branches contained in revision-range")),
		OPT_BOOL(0, "no-update-refs", &no_update_refs_flag,
			 N_("show ref update commands but do not perform them")),
		OPT_BOOL(0, "brief-stats", &brief_stats,
			 N_("show brief stats about commits/branches updated")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, replay_options, replay_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_SUBCOMMAND_OPTIONAL);

	if (fn) {
		return !!fn(argc, argv, prefix, repo);
	}

	if (!onto_name && !advance_name_opt) {
		error(_("option --onto or --advance is mandatory"));
		usage_with_options(replay_usage, replay_options);
	}

	if (advance_name_opt && contained)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--advance", "--contained");

	advance_name = xstrdup_or_null(advance_name_opt);

	repo_init_revisions(repo, &revs, prefix);

	/*
	 * Set desired values for rev walking options here. If they
	 * are changed by some user specified option in setup_revisions()
	 * below, we will detect that below and then warn.
	 *
	 * TODO: In the future we might want to either die(), or allow
	 * some options changing these values if we think they could
	 * be useful.
	 */
	revs.reverse = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	revs.simplify_history = 0;

	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1) {
		ret = error(_("unrecognized argument: %s"), argv[1]);
		goto cleanup;
	}

	/*
	 * Detect and warn if we override some user specified rev
	 * walking options.
	 */
	if (revs.reverse != 1) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"reverse");
		revs.reverse = 1;
	}
	if (revs.sort_order != REV_SORT_IN_GRAPH_ORDER) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"sort_order");
		revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	}
	if (revs.topo_order != 1) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"topo_order");
		revs.topo_order = 1;
	}
	if (revs.simplify_history != 0) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"simplify_history");
		revs.simplify_history = 0;
	}

	replayed_commits = kh_init_oid_map();
	determine_replay_mode(repo, &revs.cmdline, replayed_commits,
			      onto_name, &advance_name, &onto, &update_refs);

	/* Initialize ref transaction unless using --no-update-refs */
	if (!no_update_refs_flag) {
		unsigned int transaction_flags = batch_mode ? REF_TRANSACTION_ALLOW_FAILURE : 0;
		transaction = ref_store_transaction_begin(get_main_ref_store(repo),
								  transaction_flags,
								  &transaction_err);
		if (!transaction) {
			ret = error(_("failed to begin ref transaction: %s"), transaction_err.buf);
			goto cleanup;
		}
	}

	if (!onto) /* FIXME: Should handle replaying down to root commit */
		die("Replaying down to root commit is not supported yet!");

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto cleanup;
	}

	init_basic_merge_options(&merge_opt, repo);
	memset(&result, 0, sizeof(result));
	result.clean = 1;  /* Assume clean until proven otherwise */
	merge_opt.show_rename_progress = 0;
	last_commit = onto;
	while ((commit = get_revision(&revs))) {
		const struct name_decoration *decoration;

		if (!commit->parents)
			die(_("replaying down to root commit is not supported yet!"));
		if (!commit->parents->next)
			last_commit = pick_regular_commit(repo, commit, replayed_commits,
							  onto, &merge_opt, &result);
		else if (!commit->parents->next->next)
			last_commit = pick_merge_commit(repo, commit, replayed_commits,
							onto, &merge_opt, &result);
		else
			last_commit = pick_octopus_commit(repo, commit, replayed_commits,
							  onto, &merge_opt, &result);
		commits_updated++;

		/* TODO: Handle conflicts */
		if (!last_commit)
			die("failure to pick %s; cannot handle conflicts yet",
			       oid_to_hex(&commit->object.oid));

		/* Record commit -> pick mapping */
		record_remapping(replayed_commits,
				 &commit->object.oid, last_commit);

		/* Update any necessary branches */
		if (advance_name)
			continue;
		decoration = get_name_decoration(&commit->object);
		if (!decoration)
			continue;
		while (decoration) {
			if (decoration->type == DECORATION_REF_LOCAL &&
			    (contained || strset_contains(update_refs,
							  decoration->name))) {
				if (no_update_refs_flag) {
					printf("update %s %s %s\n",
					       decoration->name,
					       oid_to_hex(&last_commit->object.oid),
					       oid_to_hex(&commit->object.oid));
				} else {
					if (add_ref_to_transaction(transaction, decoration->name,
								   &last_commit->object.oid,
								   &commit->object.oid,
								   &transaction_err) < 0) {
						ret = error(_("failed to add ref update to transaction: %s"), transaction_err.buf);
						goto cleanup;
					}
				}
				branches_updated++;
			}
			decoration = decoration->next;
		}
	}

	/* In --advance mode, advance the target ref */
	if (result.clean == 1 && advance_name) {
		if (no_update_refs_flag) {
			printf("update %s %s %s\n",
			       advance_name,
			       oid_to_hex(&last_commit->object.oid),
			       oid_to_hex(&onto->object.oid));
		} else {
			if (add_ref_to_transaction(transaction, advance_name,
						   &last_commit->object.oid,
						   &onto->object.oid,
						   &transaction_err) < 0) {
				ret = error(_("failed to add ref update to transaction: %s"), transaction_err.buf);
				goto cleanup;
			}
		}
	}

	/* Commit the ref transaction if we have one */
	if (transaction && result.clean == 1) {
		if (ref_transaction_commit(transaction, &transaction_err)) {
			/* In atomic mode, all updates failed */
			ret = error(_("failed to update refs: %s"),
				    transaction_err.buf);
			goto cleanup;
		}
		if (brief_stats) {
			fprintf(stderr, "Updated %d commits and %d branches.\n",
				commits_updated, branches_updated);
		}
	}

	merge_finalize(&merge_opt, &result);
	kh_destroy_oid_map(replayed_commits);
	if (update_refs) {
		strset_clear(update_refs);
		free(update_refs);
	}
	ret = result.clean;

cleanup:
	if (transaction)
		ref_transaction_free(transaction);
	strbuf_release(&transaction_err);
	release_revisions(&revs);
	free(advance_name);

	/* Return */
	if (ret < 0)
		exit(128);
	return ret ? 0 : 1;
}
