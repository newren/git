/*
 * "git replay" builtin command
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "git-compat-util.h"

#include "builtin.h"
#include "commit-reach.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "oidmap.h"
#include "refs.h"
#include "revision.h"
#include "sequencer.h"
#include "strmap.h"

static struct commit *peel_committish(const char *name)
{
	struct object *obj;
	struct object_id oid;

	if (get_oid(name, &oid))
		return NULL;
	obj = parse_object(the_repository, &oid);
	return (struct commit *)peel_to_type(name, 0, obj, OBJ_COMMIT);
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

static struct commit *create_commit(struct tree *tree,
				    struct commit *based_on,
				    ... /* a bunch of struct commit * parent */)
{
	va_list parent_list;
	struct commit *parent;
	struct object_id ret;
	struct object *obj;
	struct commit_list *parents = NULL, **tmp = &parents;
	char *author;
	char *sign_commit = NULL; /* FIXME */
	struct commit_extra_header *extra;
	struct strbuf msg = STRBUF_INIT;
	const char *out_enc = get_commit_output_encoding();
	const char *message = logmsg_reencode(based_on, NULL, out_enc);
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
		return NULL;
	}
	free(author);
	strbuf_release(&msg);

	obj = parse_object(the_repository, &ret);
	return (struct commit *)obj;
}

struct ref_info {
	struct commit *onto;
	struct strset positive_refs;
	struct strset negative_refs;
	struct oidmap oids_and_refs_to_update;
	int positive_refexprs;
	int negative_refexprs;
};

static void get_ref_information(struct rev_cmdline_info *cmd_info,
				struct ref_info *ref_info)
{
	int i;

	ref_info->onto = NULL;
	strset_init(&ref_info->positive_refs);
	strset_init(&ref_info->negative_refs);
	oidmap_init(&ref_info->oids_and_refs_to_update, 0);
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
		if (dwim_ref(refexpr, strlen(refexpr), &oid, &fullname, 0) != 1)
			can_uniquely_dwim = 0;

		if (e->flags & BOTTOM) {
			if (can_uniquely_dwim)
				strset_add(&ref_info->negative_refs, fullname);
			if (!ref_info->negative_refexprs)
				ref_info->onto = lookup_commit_reference_gently(the_repository,
										&e->item->oid, 1);
			ref_info->negative_refexprs++;
		} else {
			if (can_uniquely_dwim) {
				struct string_list *refs;

				strset_add(&ref_info->positive_refs, fullname);
				refs = oidmap_get_field(&ref_info->oids_and_refs_to_update,
							&oid);
				if (!refs) {
					refs = xmalloc(sizeof(*refs));
					string_list_init_dup(refs);
				}
				string_list_append(refs, fullname);
				oidmap_put_field(&ref_info->oids_and_refs_to_update,
						 &oid, refs);
			}
			ref_info->positive_refexprs++;
		}

		free(fullname);
	}
}

static void free_oidrefmap(struct oidmap *oids_and_refs_to_update)
{
	struct hashmap_iter iter;
	struct oidmap_field_entry *entry;
	struct string_list *refs;

	if (!oids_and_refs_to_update)
		return;

	oidmap_for_each_entry(oids_and_refs_to_update, &iter, entry) {
		refs = entry->field;
		string_list_clear(refs, 0);
		free(refs);
	}
	oidmap_free(oids_and_refs_to_update, 0);
}

static void determine_replay_mode(struct rev_cmdline_info *cmd_info,
				  const char *onto_name,
				  const char **advance_name,
				  struct commit **onto,
				  struct oidmap **oids_and_refs_to_update)
{
	struct ref_info rinfo;

	get_ref_information(cmd_info, &rinfo);
	if (!rinfo.positive_refexprs)
		die(_("need some commits to replay"));
	if (onto_name && *advance_name)
		die(_("--onto and --advance are incompatible"));
	else if (onto_name) {
		*onto = peel_committish(onto_name);
		if (rinfo.positive_refexprs <
		    strset_get_size(&rinfo.positive_refs))
			die(_("all positive revisions given must be references"));
	} else if (*advance_name) {
		struct object_id oid;
		char *fullname = NULL;

		*onto = peel_committish(*advance_name);
		if (dwim_ref(*advance_name, strlen(*advance_name),
			     &oid, &fullname, 0) == 1) {
			*advance_name = fullname;
		} else {
			die(_("argument to --advance must be a reference"));
		}
		if (rinfo.positive_refexprs > 1)
			die(_("cannot advance target with multiple source branches because ordering would be ill-defined"));
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

			if (rinfo.negative_refexprs == 0)
				die(_("all positive revisions given must be references"));
			else if (rinfo.negative_refexprs > 1)
				die(_("cannot implicitly determine whether this is an --advance or --onto operation"));
			else if (rinfo.positive_refexprs > 1)
				die(_("cannot advance target with multiple source branches because ordering would be ill-defined"));

			/* Only one entry, but we have to loop to get it */
			strset_for_each_entry(&rinfo.negative_refs,
					      &iter, entry) {
				*advance_name = entry->key;
			}
		} else { /* positive_refs_complete */
			if (rinfo.negative_refexprs > 1)
				die(_("cannot implicitly determine correct base for --onto"));
			if (rinfo.negative_refexprs == 1)
				*onto = rinfo.onto;
		}
	}
	if (!*advance_name) {
		size_t memsize = sizeof(**oids_and_refs_to_update);
		*oids_and_refs_to_update = xcalloc(1, memsize);
		**oids_and_refs_to_update = rinfo.oids_and_refs_to_update;
		memset(&rinfo.oids_and_refs_to_update, 0, memsize);
	}

	/* cleanup */
	strset_clear(&rinfo.negative_refs);
	strset_clear(&rinfo.positive_refs);
	free_oidrefmap(&rinfo.oids_and_refs_to_update);
}

static struct commit *mapped_commit(kh_oid_map_t *replayed_commits,
				    struct commit *commit,
				    struct commit *fallback)
{
	khint_t pos = kh_get_oid_map(replayed_commits, commit->object.oid);
	if (pos == kh_end(replayed_commits))
		return fallback;
	return kh_value(replayed_commits, pos);
}

static struct commit *pick_regular_commit(struct commit *pickme,
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

	result->tree = get_commit_tree(replayed_base);
	pickme_tree = get_commit_tree(pickme);
	base_tree = get_commit_tree(base);

	ctx.abbrev = DEFAULT_ABBREV;
	format_commit_message(replayed_base, "%h (%s)", &parent1_desc, &ctx);
	format_commit_message(pickme,        "%h (%s)", &parent2_desc, &ctx);
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

	return create_commit(result->tree, pickme, replayed_base, NULL);
}

static void do_merge(struct commit *parent1,
		     struct commit *parent2,
		     struct merge_options *o,
		     struct merge_result *result)
{
	/* Caller must call merge_finalize */
	struct commit_list *bases;

	/* Setup merge options */
	init_merge_options(o, the_repository);
	o->show_rename_progress = 0;
	o->record_conflict_msgs_as_headers = 1;
	o->msg_header_prefix = "remerge";

	o->branch1 = "parent1"; /* TODO */
	o->branch2 = "parent2";

	/* Parse the relevant commits and get the merge bases */
	//parse_commit_or_die(parent1);
	//parse_commit_or_die(parent2);
	bases = get_merge_bases(parent1, parent2);

	/* Re-merge the parents */
	merge_incore_recursive(o, bases, parent1, parent2, result);
}

static struct commit *pick_merge_commit(struct commit *pickme,
					kh_oid_map_t *replayed_commits,
					struct commit *onto,
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
	replayed_par1 = mapped_commit(replayed_commits, parent1, onto);
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

	do_merge(parent1, parent2, &remerge_opt, &remerge_res);
	do_merge(replayed_par1, replayed_par2, &new_merge_opt, &new_merge_res);

	remerge_tree = remerge_res.tree;
	pickme_tree = get_commit_tree(pickme);
	new_merge_tree = new_merge_res.tree;

	ctx.abbrev = DEFAULT_ABBREV;
	format_commit_message(pickme, "remerge of %h (%s)",
			      &remerge_desc, &ctx);
	format_commit_message(pickme, "%h (%s)",
			      &pickme_desc, &ctx);
	format_commit_message(pickme, "new merge of %h (%s)",
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
	if (result->clean != 1 ||
	    remerge_res.clean != 1 ||
	    new_merge_res.clean != 1)
		return NULL;

	return create_commit(result->tree, pickme,
			     replayed_par1, replayed_par2, NULL);
}

static struct commit *pick_octopus_commit(struct commit *pickme,
					  kh_oid_map_t *replayed_commits,
					  struct commit *onto,
					  struct merge_options *merge_opt,
					  struct merge_result *result)
{
	/* Remember: do not capitalize first letter of the error message! */
	BUG("nOT IMPLEMENTED!!!");
}

/* replay without making an interactive script; not restartable */
static int one_shot_replay(const char *advance_name,
			   const char *onto_name,
			   int contained,
			   int argc,
			   const char **argv,
			   const char *prefix)
{
	struct rev_info revs;
	struct commit *onto = NULL;
	struct commit *pick = NULL;
	struct commit *commit;
	struct merge_options merge_opt;
	struct merge_result result;
	struct oidmap *oid_ref_map = NULL;
	kh_oid_map_t *replayed_commits;

	repo_init_revisions(the_repository, &revs, prefix);

	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1)
		die(_("unrecognized argument: %s"), argv[1]);

	/* requirements/overrides for revs */
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	revs.reverse = 1;
	revs.simplify_history = 0;

	determine_replay_mode(&revs.cmdline, onto_name, &advance_name,
			      &onto, &oid_ref_map);

	if (prepare_revision_walk(&revs) < 0)
		return error(_("error preparing revisions"));

	init_merge_options(&merge_opt, the_repository);
	memset(&result, 0, sizeof(result));
	merge_opt.show_rename_progress = 0;
	assert(onto); /* FIXME: Should handle replaying down to root commit */
	pick = onto;
	replayed_commits = kh_init_oid_map();
	while ((commit = get_revision(&revs))) {
		khint_t pos;
		int hr;
		struct string_list *refs;
		struct string_list_item *item;

		/* Pick the commit */
		if (!commit->parents)
			/* TODO: Handle root commits */
			BUG("Cannot handle root commits yet!");
		if (!commit->parents->next)
			pick = pick_regular_commit(commit, replayed_commits,
						   onto, &merge_opt, &result);
		else if (!commit->parents->next->next)
			pick = pick_merge_commit(commit, replayed_commits,
						 onto, &merge_opt, &result);
		else
			pick = pick_octopus_commit(commit, replayed_commits,
						   onto, &merge_opt, &result);

		if (!pick) {
			/* TODO: handle conflicts in sparse worktree instead */
			struct object_id head;
			struct tree *head_tree;
			struct lock_file lock = LOCK_INIT;

			hold_locked_index(&lock, LOCK_DIE_ON_ERROR);
			if (repo_read_index(the_repository) < 0)
				BUG("Could not read index");

			get_oid("HEAD", &head);
			head_tree = parse_tree_indirect(&head);
			printf("Switching from %s to %s.\n",
			       oid_to_hex(&head_tree->object.oid),
			       oid_to_hex(&result.tree->object.oid));
			merge_switch_to_result(&merge_opt, head_tree, &result,
					       1, 1);
			if (write_locked_index(&the_index, &lock,
					       COMMIT_LOCK | SKIP_IF_UNCHANGED))
				die(_("unable to write %s"), get_index_file());

			die("failure to pick %s; cannot handle conflicts yet",
			       oid_to_hex(&commit->object.oid));
		}

		/* Record commit -> pick mapping */
		pos = kh_put_oid_map(replayed_commits, commit->object.oid, &hr);
		if (hr == 0)
			BUG("Duplicate rewritten commit: %s\n",
			    oid_to_hex(&commit->object.oid));
		kh_value(replayed_commits, pos) = pick;

		/* Update any necessary branches */
		refs = get_refs_to_update(commit, oid_ref_map, contained);
		if (!refs)
			continue;
		for_each_string_list_item(item, refs) {
			printf("update %s %s %s\n",
			       item->string,
			       oid_to_hex(&pick->object.oid),
			       oid_to_hex(&commit->object.oid));
		}
		string_list_clear(refs, 0);
		free(refs);
	}

	/* In --advance mode, advance the target ref */
	if (result.clean == 1 && advance_name) {
		printf("update %s %s %s\n",
		       advance_name,
		       oid_to_hex(&pick->object.oid),
		       oid_to_hex(&onto->object.oid));
	}

	/* Cleanup */
	kh_destroy_oid_map(replayed_commits);
	free_oidrefmap(oid_ref_map);
	memset(&revs, 0, sizeof(revs)); /* TODO: write&call rev_info_free()? */
	merge_finalize(&merge_opt, &result);

	/* Return */
	if (result.clean < 0)
		exit(128);
	return result.clean;
}

static int interactive_restartable_replay(const char *advance_name,
					  const char *onto_name,
					  int contained,
					  int argc,
					  const char **argv,
					  const char *prefix)
{
	struct todo_list todo_list = TODO_LIST_INIT;
	int flags;

	struct rev_info revs;
	struct commit *onto = NULL;
	struct oidmap *oid_ref_map = NULL;

	int use_oldstyle_rebase_merges = 0;
	if (!use_oldstyle_rebase_merges) {

	repo_init_revisions(the_repository, &revs, prefix);

	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1)
		die(_("unrecognized argument: %s"), argv[1]);

	/* requirements/overrides for revs */
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	revs.reverse = 1;
	revs.simplify_history = 0;

	determine_replay_mode(&revs.cmdline, onto_name, &advance_name,
			      &onto, &oid_ref_map);

	if (prepare_revision_walk(&revs) < 0)
		return error(_("error preparing revisions"));

	if (make_replay_script(&todo_list.buf, &revs,
			       onto, advance_name, oid_ref_map,
			       contained))
		die(_("could not generate todo list"));

	} else {

	flags = TODO_LIST_APPEND_TODO_HELP |
		TODO_LIST_SHORTEN_IDS |
		//TODO_LIST_REPLAY;
		TODO_LIST_REBASE_MERGES | TODO_LIST_REBASE_COUSINS;
	if (sequencer_make_script(the_repository, &todo_list.buf,
				  argc, argv, flags))
		die(_("could not generate todo list"));
	}

	puts(todo_list.buf.buf);
	die("I quit.");
}

int cmd_replay(int argc, const char **argv, const char *prefix)
{
	const char *advance_name = NULL;
	const char *onto_name = NULL;
	int contained = 0;
	int clean;

	const char * const replay_usage[] = {
		N_("git replay [--onto <newbase> | --advance <branch>] <revision-range>"),
		NULL
	};
	struct option replay_options[] = {
		OPT_STRING(0, "advance", &advance_name,
			   N_("branch"),
			   N_("make replay advance given branch")),
		OPT_STRING(0, "onto", &onto_name,
			   N_("revision"),
			   N_("replay onto given commit")),
		OPT_BOOL(0, "contained", &contained,
			 N_("advance all branches contained in revision-range")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(replay_usage, replay_options);

	argc = parse_options(argc, argv, prefix, replay_options, replay_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN);

	if (advance_name && contained)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--advance", "--contained");

	if (0) {
		clean = one_shot_replay(advance_name, onto_name, contained,
					argc, argv, prefix);
	} else {
		clean = interactive_restartable_replay(advance_name, onto_name,
						       contained,
						       argc, argv, prefix);
	}
	return clean ? 0 : 1;
}
