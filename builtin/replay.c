/*
 * "git replay" builtin command
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "git-compat-util.h"

#include "builtin.h"
#include "merge-ort.h"
#include "refs.h"
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

static struct commit *guess_new_base(struct rev_cmdline_info *info)
{
	struct commit *new_base = NULL;
	int i, bottom_commits = 0;

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
	for (i = 0; i < info->nr; i++) {
		struct rev_cmdline_entry *e = info->rev + i;
		struct object_id oid;
		char *fullname = NULL;

		if (!(e->flags & BOTTOM))
			continue;

		/*
		 * We need a unique base commit to know where to replay; error
		 * out if not unique.
		 *
		 * Also, we usually don't want to replay commits on the same
		 * base they started on, so only accept this as the base if
		 * it uniquely names some ref.
		 */
		if (bottom_commits++ ||
		    dwim_ref(e->name, strlen(e->name), &oid, &fullname, 0) != 1)
			die(_("cannot determine where to replay commits; please specify --onto"));

		free(fullname);
		new_base = lookup_commit_reference_gently(the_repository,
							  &e->item->oid, 1);
	}

	return new_base;
}

int cmd_replay(int argc, const char **argv, const char *prefix)
{
	const char *onto_name = NULL;
	struct commit *onto = NULL;
	struct commit *last_commit = NULL;
	struct rev_info revs;
	struct commit *commit;
	struct merge_options merge_opt;
	struct tree *next_tree, *base_tree;
	struct merge_result result;

	const char * const replay_usage[] = {
		N_("git replay [--onto <newbase>] <revision-range>"),
		NULL
	};
	struct option replay_options[] = {
		OPT_STRING(0, "onto", &onto_name,
			   N_("revision"),
			   N_("replay onto given commit")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(replay_usage, replay_options);

	argc = parse_options(argc, argv, prefix, replay_options, replay_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN);

	repo_init_revisions(the_repository, &revs, prefix);
	/* defaults for revs */
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;

	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1)
		die(_("unrecognized argument: %s"), argv[1]);
	/* requirements for revs */
	revs.reverse = 1;

	if (onto_name)
		onto = peel_committish(onto_name);
	else
		onto = guess_new_base(&revs.cmdline);

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
