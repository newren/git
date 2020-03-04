/*
 * "git fast-rebase" builtin command
 *
 * FAST: Forking Any Subprocesses (is) Taboo
 *
 * This is meant SOLELY as a demo of what is possible.  sequencer.c and
 * rebase.c should be refactored to use the ideas here, rather than attempting
 * to extend this file to replace those (unless Phillip or Dscho say that
 * refactoring is too hard and we need a clean slate, but I'm guessing that
 * refactoring is the better route).
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "builtin.h"

#include "cache-tree.h"
#include "commit.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "parse-options.h"
#include "revision.h"
#include "strvec.h"
#include "tree.h"

static const char *short_commit_name(struct commit *commit)
{
	return find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV);
}

int cmd_fast_rebase(int argc, const char **argv, const char *prefix)
{
	struct commit *branch, *onto, *upstream;
	const char * const usage[] = {
		N_("read the code, figure out how to use it, then do so"),
		NULL
	};
	struct option options[] = {
		{ OPTION_CALLBACK, 0, "onto", &onto, N_("onto"), N_("onto"),
		  PARSE_OPT_NONEG, parse_opt_commit, 0 },
		{ OPTION_CALLBACK, 0, "upstream", &upstream, N_("upstream"),
		  N_("the upstream commit"), PARSE_OPT_NONEG, parse_opt_commit,
		  0 },
		{ OPTION_CALLBACK, 0, "head", &branch, N_("head-name"),
		  N_("head name"), PARSE_OPT_NONEG, parse_opt_commit, 0 },
		OPT_END()
	};
	struct object_id head;
	struct lock_file lock_file = LOCK_INIT;
	int fd, clean = 1;
	struct strvec rev_walk_args = STRVEC_INIT;
	struct rev_info revs;
	struct commit *commit;
	struct merge_options merge_opt;
	struct tree *next_tree, *base_tree, *head_tree;
	struct merge_result result;

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	/* Sanity check */
	if (get_oid("HEAD", &head))
		die(_("Cannot read HEAD"));
	assert(oideq(&onto->object.oid, &head));

	fd = hold_locked_index(&lock_file, 0);
	if (repo_read_index(the_repository) < 0)
		die(_("could not read index"));
	refresh_index(the_repository->index, REFRESH_QUIET, NULL, NULL, NULL);
	if (0 <= fd)
		repo_update_index_if_able(the_repository, &lock_file);
	rollback_lock_file(&lock_file);

	repo_init_revisions(the_repository, &revs, NULL);
	revs.verbose_header = 1;
	revs.max_parents = 1;
	revs.cherry_mark = 1;
	revs.limited = 1;
	revs.reverse = 1;
	revs.right_only = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	strvec_pushl(&rev_walk_args, "", head, "--not", upstream, NULL);
I)

	if (setup_revisions(rev_walk_args.nr, rev_walk_args.v, &revs, NULL) > 1)
		return error(_("make_script: unhandled options"));

	strvec_clear(&rev_walk_args);

	if (prepare_revision_walk(&revs) < 0)
		return error(_("make_script: error preparing revisions"));

	init_merge_options(&merge_opt, the_repository);
	memset(&result, 0, sizeof(result));
	merge_opt.show_rename_progress = 1;
	merge_opt.branch1 = "HEAD";
	head_tree = get_commit_tree(onto);
	result.automerge_tree = head_tree;
	while ((commit = get_revision(&revs))) {
		struct commit *base;

		assert(commit->parents && !commit->parents->next);
		base = commit->parents->item;

		next_tree = get_commit_tree(commit);
		base_tree = get_commit_tree(base);

		merge_opt.branch2 = short_commit_name(commit);
		merge_opt.ancestor = xstrfmt("parent of %s", merge_opt.branch2);

		merge_ort_inmemory_nonrecursive(&merge_opt,
						result.automerge_tree,
						next_tree,
						base_tree,
						&result);
	}

	if (switch_to_merge_result(&merge_opt, head_tree, &result))
		clean = -1;
	merge_finalize(&merge_opt, &result);

	return (clean == 0);
}
