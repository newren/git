#include "cache.h"
#include "merge-ort.h"
#include "merge-ort-wrappers.h"

#include "commit.h"

static int unclean(struct merge_options *opt, struct tree *head)
{
	/* Sanity check on repo state; index must match head */
	struct strbuf sb = STRBUF_INIT;

	if (head && repo_index_has_changes(opt->repo, head, &sb)) {
		fprintf(stderr, _("Your local changes to the following files would be overwritten by merge:\n  %s"),
		    sb.buf);
		strbuf_release(&sb);
		return -1;
	}

	return 0;
}

int merge_ort_nonrecursive(struct merge_options *opt,
			   struct tree *head,
			   struct tree *merge,
			   struct tree *merge_base)
{
	struct merge_result result;

	if (unclean(opt, head))
		return -1;
	merge_inmemory_nonrecursive(opt, head, merge, merge_base, &result);
	merge_switch_to_result(opt, head, &result, 1, 1);

	return result.clean;
}

int merge_ort_recursive(struct merge_options *opt,
			struct commit *side1,
			struct commit *side2,
			struct commit_list *merge_bases,
			struct tree **result)
{
	struct tree *head = repo_get_commit_tree(opt->repo, side1);
	struct merge_result tmp;

	if (unclean(opt, head))
		return -1;

	merge_inmemory_recursive(opt, side1, side2, merge_bases, &tmp);
	merge_switch_to_result(opt, head, &tmp, 1, 1);
	*result = tmp.automerge_tree;

	return tmp.clean;
}
