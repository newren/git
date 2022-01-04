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

static void get_negative_and_positive_refs(struct rev_cmdline_info *info,
					   struct commit **onto,
					   struct string_list *replay_refs)
{
	int i;
	struct commit *provisional_onto = NULL;

	char *descriptions[] = {
			"REV_CMD_REF",
			"REV_CMD_PARENTS_ONLY",
			"REV_CMD_LEFT",
			"REV_CMD_RIGHT",
			"REV_CMD_MERGE_BASE",
			"REV_CMD_REV" };

	for (i = 0; i < info->nr; i++) {
		struct rev_cmdline_entry *e = info->rev + i;
		struct object_id oid;
		//struct commit *commit;
		char *full_name = NULL;
		int can_uniquely_dwim = 1;

		if (dwim_ref(e->name, strlen(e->name), &oid, &full_name, 0) != 1)
			can_uniquely_dwim = 0;

		printf("%2d %s %20s %04x %s %s\n",
		       i, oid_to_hex(&e->item->oid),
		       descriptions[e->whence], e->flags,
		       e->name, full_name);
		if (!(e->flags & BOTTOM)) {
			/* positive ref we need to update */
			if (can_uniquely_dwim)
				string_list_append(replay_refs, full_name);
		} else if (!*onto) {
			if (provisional_onto)
				die(_("cannot determine where to replay commits; please specify --onto"));
			else
				provisional_onto = lookup_commit_reference_gently(the_repository, &e->item->oid, 1);
		}

		free(full_name);
	}
	if (provisional_onto)
		*onto = provisional_onto;

	string_list_sort(replay_refs);

#if 0
		if (!*revision_sources_at(&revision_sources, commit))
			*revision_sources_at(&revision_sources, commit) = full_name;
#endif
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
	struct string_list replay_refs = STRING_LIST_INIT_DUP;
	int i;

	const char * const replay_usage[] = {
		N_("git replay [--onto <newbase>] <revision-range>"),
		NULL
	};
	struct option replay_options[] = {
		OPT_STRING(0, "onto", &onto_name,
			   N_("revision"),
			   N_("rebase onto given commit")),
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

	printf("Before, onto=%s, replay_refs.nr=%d\n",
	       onto_name ? oid_to_hex(&onto->object.oid) : "NULL", replay_refs.nr);
	get_negative_and_positive_refs(&revs.cmdline, &onto, &replay_refs);
	printf("After:\n");
	printf("  onto=%s\n", onto ? oid_to_hex(&onto->object.oid) : "NULL");
	for (i = 0; i < replay_refs.nr; i++)
		printf("  replay_refs[%d] = %s\n", i, replay_refs.items[i].string);
	exit(0);

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
