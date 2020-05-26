#ifndef MERGE_ORT_H
#define MERGE_ORT_H

#include "merge-recursive.h"

struct commit;
struct tree;

struct merge_result {
	/* whether the merge is clean */
	int clean;

	/* Result of merge.  If !clean, represents what would go in worktree */
	struct tree *tree;

	/* merge status messages (conflict notices, automerging files, etc.) */
	struct strbuf *output; /* NULL means ignore output? */

	/*
	 * Additional metadata used by merge_switch_to_result() or future calls
	 * to merge_ort_inmemory_*().
	 */
	void *priv;
};

/* rename-detecting three-way merge, no recursion. */
void merge_inmemory_recursive(struct merge_options *opt,
			      struct commit *side1,
			      struct commit *side2,
			      struct commit_list *merge_bases,
			      struct merge_result *result);

/* rename-detecting three-way merge with recursive ancestor consolidation. */
void merge_inmemory_nonrecursive(struct merge_options *opt,
				 struct tree *side1,
				 struct tree *side2,
				 struct tree *merge_base,
				 struct merge_result *result);

/* Update the working tree and index from head to result after inmemory merge */
void merge_switch_to_result(struct merge_options *opt,
			    struct tree *head,
			    struct merge_result *result,
			    int update_worktree_and_index,
			    int display_update_msgs);

/* Do needed cleanup when not calling merge_switch_to_result() */
void merge_finalize(struct merge_options *opt,
		    struct merge_result *result);

#endif
