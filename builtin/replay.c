/*
 * "git replay" builtin command
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "git-compat-util.h"

#include "builtin.h"
#include "merge-ort.h"
#include "revision.h"
#include "strvec.h"

static const char *short_commit_name(struct commit *commit)
{
	return find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV);
}

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
				    struct commit *parent)
{
	struct object_id ret;
	struct object *obj;
	struct commit_list *parents = NULL;
	char *author;
	char *sign_commit = NULL;
	struct commit_extra_header *extra;
	struct strbuf msg = STRBUF_INIT;
	const char *out_enc = get_commit_output_encoding();
	const char *message = logmsg_reencode(based_on, NULL, out_enc);
	const char *orig_message = NULL;
	const char *exclude_gpgsig[] = { "gpgsig", NULL };

	commit_list_insert(parent, &parents);
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

int cmd_replay(int argc, const char **argv, const char *prefix)
{
	struct commit *onto;
	struct commit *last_commit = NULL;
	struct strvec rev_walk_args = STRVEC_INIT;
	struct rev_info revs;
	struct commit *commit;
	struct merge_options merge_opt;
	struct tree *next_tree, *base_tree;
	struct merge_result result;
	struct strbuf branch_name = STRBUF_INIT;

	if (argc == 2 && !strcmp(argv[1], "-h")) {
		printf("Sorry, I am not a psychiatrist; I can not give you the help you need.  Oh, you meant usage...\n");
		exit(129);
	}

	if (argc != 5 || strcmp(argv[1], "--onto"))
		die("usage: read the code, figure out how to use it, then do so");

	onto = peel_committish(argv[2]);
	strbuf_addf(&branch_name, "refs/heads/%s", argv[4]);

	repo_init_revisions(the_repository, &revs, NULL);
	revs.verbose_header = 1;
	revs.max_parents = 1;
	revs.cherry_mark = 1;
	revs.limited = 1;
	revs.reverse = 1;
	revs.right_only = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	strvec_pushl(&rev_walk_args, "", argv[4], "--not", argv[3], NULL);

	if (setup_revisions(rev_walk_args.nr, rev_walk_args.v, &revs, NULL) > 1)
		return error(_("unhandled options"));

	strvec_clear(&rev_walk_args);

	if (prepare_revision_walk(&revs) < 0)
		return error(_("error preparing revisions"));

	init_merge_options(&merge_opt, the_repository);
	memset(&result, 0, sizeof(result));
	merge_opt.show_rename_progress = 0;
	result.tree = get_commit_tree(onto);
	last_commit = onto;
	while ((commit = get_revision(&revs))) {
		struct commit *base;

		assert(commit->parents && !commit->parents->next);
		base = commit->parents->item;

		next_tree = get_commit_tree(commit);
		base_tree = get_commit_tree(base);

		merge_opt.branch1 = short_commit_name(commit);
		merge_opt.branch2 = short_commit_name(commit);
		merge_opt.ancestor = xstrfmt("parent of %s", merge_opt.branch2);

		merge_incore_nonrecursive(&merge_opt,
					  base_tree,
					  result.tree,
					  next_tree,
					  &result);

		free((char*)merge_opt.ancestor);
		merge_opt.ancestor = NULL;
		if (!result.clean)
			break;
		last_commit = create_commit(result.tree, commit, last_commit);
	}

	/* Output */
	printf("%s\n", oid_to_hex(&last_commit->object.oid));
	if (result.clean == 0)
		printf("%s\n", oid_to_hex(&commit->object.oid));

	/* Cleanup */
	memset(&revs, 0, sizeof(revs)); /* TODO: write&call rev_info_free()? */
	merge_finalize(&merge_opt, &result);

	/* Return */
	if (result.clean < 0)
		exit(128);
	return result.clean ? 0 : 1;
}
