#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "commit-reach.h"
#include "environment.h"
#include "hex.h"
#include "merge-ort.h"
#include "object-name.h"
#include "refs.h"
#include "replay.h"
#include "revision.h"
#include "strmap.h"
#include "tree.h"

static struct commit *peel_committish(struct repository *repo,
				      const char *name,
				      const char *mode)
{
	struct object *obj;
	struct object_id oid;

	if (repo_get_oid(repo, name, &oid))
		die(_("'%s' is not a valid commit-ish for %s"), name, mode);
	obj = parse_object_or_die(repo, &oid, name);
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
	const char *exclude_gpgsig[] = { "gpgsig", "gpgsig-sha256", NULL };

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
	size_t positive_refexprs;
	size_t negative_refexprs;
};

static void get_ref_information(struct repository *repo,
				struct rev_cmdline_info *cmd_info,
				struct ref_info *ref_info)
{
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
	for (size_t i = 0; i < cmd_info->nr; i++) {
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
		} else {
			if (can_uniquely_dwim)
				strset_add(&ref_info->positive_refs, fullname);
			ref_info->positive_refexprs++;
		}

		free(fullname);
	}
}

static void set_up_replay_mode(struct repository *repo,
			       struct rev_cmdline_info *cmd_info,
			       const char *onto_name,
			       bool *detached_head,
			       char **advance_name,
			       struct commit **onto,
			       struct strset **update_refs)
{
	struct ref_info rinfo;
	int head_flags = 0;

	refs_read_ref_full(get_main_ref_store(repo), "HEAD",
			   RESOLVE_REF_NO_RECURSE, NULL, &head_flags);
	*detached_head = !(head_flags & REF_ISSYMREF);

	get_ref_information(repo, cmd_info, &rinfo);
	if (!rinfo.positive_refexprs)
		die(_("need some commits to replay"));

	if (!onto_name == !*advance_name)
		BUG("one and only one of onto_name and *advance_name must be given");

	if (onto_name) {
		*onto = peel_committish(repo, onto_name, "--onto");
		if (rinfo.positive_refexprs <
		    strset_get_size(&rinfo.positive_refs))
			die(_("all positive revisions given must be references"));
		*update_refs = xcalloc(1, sizeof(**update_refs));
		**update_refs = rinfo.positive_refs;
		memset(&rinfo.positive_refs, 0, sizeof(**update_refs));
	} else {
		struct object_id oid;
		char *fullname = NULL;

		if (!*advance_name)
			BUG("expected either onto_name or *advance_name in this function");

		if (repo_dwim_ref(repo, *advance_name, strlen(*advance_name),
			     &oid, &fullname, 0) == 1) {
			free(*advance_name);
			*advance_name = fullname;
		} else {
			die(_("argument to --advance must be a reference"));
		}
		*onto = peel_committish(repo, *advance_name, "--advance");
		if (rinfo.positive_refexprs > 1)
			die(_("cannot advance target with multiple sources because ordering would be ill-defined"));
	}
	strset_clear(&rinfo.negative_refs);
	strset_clear(&rinfo.positive_refs);
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

static struct commit *pick_regular_commit(struct repository *repo,
					  struct commit *pickme,
					  struct commit *replayed_base,
					  kh_oid_map_t *replayed_commits,
					  struct commit *onto,
					  struct merge_options *merge_opt,
					  struct merge_result *result)
{
	struct commit *base;
	struct tree *pickme_tree, *base_tree, *replayed_base_tree;
	struct pretty_print_context ctx = {0};
	struct strbuf parent1_desc = STRBUF_INIT;
	struct strbuf parent2_desc = STRBUF_INIT;

	base = pickme->parents ? pickme->parents->item : NULL;
	if (!replayed_base)
		replayed_base = base ?
			mapped_commit(replayed_commits, base, onto) :
			onto;

	replayed_base_tree = repo_get_commit_tree(repo, replayed_base);
	pickme_tree = repo_get_commit_tree(repo, pickme);
	base_tree = base ?
		repo_get_commit_tree(repo, base) :
		lookup_tree(merge_opt->repo, merge_opt->repo->hash_algo->empty_tree);

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
				  replayed_base_tree,
				  pickme_tree,
				  result);

	free((char*)merge_opt->ancestor);
	merge_opt->ancestor = NULL;
	strbuf_release(&parent1_desc);
	strbuf_release(&parent2_desc);
	if (!result->clean)
		return NULL;
	/* Drop commits that become empty */
	if (oideq(&replayed_base_tree->object.oid, &result->tree->object.oid) &&
	    !oideq(&pickme_tree->object.oid, &base_tree->object.oid))
		return replayed_base;

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
	if (result->clean != 1 ||
	    remerge_res.clean != 1 ||
	    new_merge_res.clean != 1)
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

void replay_result_release(struct replay_result *result)
{
	for (size_t i = 0; i < result->updates_nr; i++)
		free(result->updates[i].refname);
	free(result->updates);
}

static void replay_result_queue_update(struct replay_result *result,
				       const char *refname,
				       const struct object_id *old_oid,
				       const struct object_id *new_oid)
{
	ALLOC_GROW(result->updates, result->updates_nr + 1, result->updates_alloc);
	result->updates[result->updates_nr].refname = xstrdup(refname);
	result->updates[result->updates_nr].old_oid = *old_oid;
	result->updates[result->updates_nr].new_oid = *new_oid;
	result->updates_nr++;
}

int replay_revisions(struct rev_info *revs,
		     struct replay_revisions_options *opts,
		     struct replay_result *out)
{
	kh_oid_map_t *replayed_commits = NULL;
	struct strset *update_refs = NULL;
	struct commit *last_commit = NULL;
	struct commit *commit;
	struct commit *onto = NULL;
	struct merge_options merge_opt;
	struct merge_result result = {
		.clean = 1,
	};
	bool detached_head;
	char *advance;
	int ret;

	advance = xstrdup_or_null(opts->advance);
	set_up_replay_mode(revs->repo, &revs->cmdline, opts->onto,
			   &detached_head, &advance, &onto, &update_refs);

	if (prepare_revision_walk(revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto out;
	}

	init_basic_merge_options(&merge_opt, revs->repo);
	merge_opt.show_rename_progress = 0;
	last_commit = onto;
	replayed_commits = kh_init_oid_map();
	while ((commit = get_revision(revs))) {
		const struct name_decoration *decoration;
		khint_t pos;
		int hr;
		bool is_merge = commit->parents && commit->parents->next;

		if (opts->linearize && is_merge)
			/*
			 * drop the merge commit, then map it to the same
			 * 'last_commit' as previously used
			 */
			goto update_mapping;
		else if (!commit->parents || !commit->parents->next)
			last_commit =
				pick_regular_commit(revs->repo, commit,
						    opts->linearize ? last_commit : NULL,
						    replayed_commits, onto,
						    &merge_opt, &result);
		else if (!commit->parents->next->next)
			last_commit =
				pick_merge_commit(revs->repo, commit,
						  replayed_commits,
						  onto, &merge_opt, &result);
		else
			last_commit =
				pick_octopus_commit(revs->repo, commit,
						    replayed_commits,
						    onto, &merge_opt, &result);

		if (!last_commit)
			/* TODO: handle conflicts */
			break;

update_mapping:
		/* Record commit -> last_commit mapping */
		pos = kh_put_oid_map(replayed_commits, commit->object.oid, &hr);
		if (hr == 0)
			BUG("Duplicate rewritten commit: %s\n",
			    oid_to_hex(&commit->object.oid));
		kh_value(replayed_commits, pos) = last_commit;

		/* Update any necessary branches */
		if (advance)
			continue;

		for (decoration = get_name_decoration(&commit->object);
		     decoration;
		     decoration = decoration->next)
		{
			if (decoration->type != DECORATION_REF_LOCAL &&
			    decoration->type != DECORATION_REF_HEAD)
				continue;

			/*
			 * We only need to update HEAD separately in case it's
			 * detached. If it's not we'd already update the branch
			 * it is pointing to.
			 */
			if (decoration->type == DECORATION_REF_HEAD && !detached_head)
				continue;

			if (!opts->contained &&
			    !strset_contains(update_refs, decoration->name))
				continue;

			replay_result_queue_update(out, decoration->name,
						   &commit->object.oid,
						   &last_commit->object.oid);
		}
	}

	if (!result.clean) {
		ret = 1;
		goto out;
	}

	/* In --advance mode, advance the target ref */
	if (advance)
		replay_result_queue_update(out, advance,
					   &onto->object.oid,
					   &last_commit->object.oid);

	ret = 0;

out:
	if (update_refs) {
		strset_clear(update_refs);
		free(update_refs);
	}
	kh_destroy_oid_map(replayed_commits);
	merge_finalize(&merge_opt, &result);
	free(advance);
	return ret;
}
