#ifndef MERGE_ORT_H
#define MERGE_ORT_H

#include "merge-recursive.h"
#include "strmap.h"
#include "tree.h"

struct merge_result {
	/* whether the merge is clean */
	int clean:1;

	/* automerge result.  If !clean, represents what would go in worktree */
	struct tree *automerge_tree;

	/*
	 * Additional metadata used by switch_to_merge_result() or future calls
	 * to merge_ort_inmemory_*().
	 */
	void *priv;
};

/* rename-detecting three-way merge, no recursion. */
void merge_ort_inmemory_recursive(struct merge_options *opt,
				  struct commit *side1,
				  struct commit *side2,
				  struct commit_list *merge_bases,
				  struct merge_result *result);

/* rename-detecting three-way merge with recursive ancestor consolidation. */
void merge_ort_inmemory_nonrecursive(struct merge_options *opt,
				     struct tree *head,
				     struct tree *merge,
				     struct tree *merge_base,
				     struct merge_result *result);

/* Update the working tree and index after an inmemory merge */
int switch_to_merge_result(struct merge_options *opt,
			   struct tree *head,
			   struct merge_result *result);

/* Free memory used by merge_result */
void merge_finalize(struct merge_options *opt,
		    struct merge_result *result);


/*** Wrappers for convenience of porting old code: ***/


/*
 * rename-detecting three-way merge, no recursion.
 * Wrapper mimicking the old merge_trees() function.
 */
int merge_ort_nonrecursive(struct merge_options *opt,
			   struct tree *head,
			   struct tree *merge,
			   struct tree *common);

/*
 * rename-detecting three-way merge with recursive ancestor consolidation.
 * Wrapper mimicking the old merge_recursive() function.
 */
int merge_ort_recursive(struct merge_options *opt,
			struct commit *h1,
			struct commit *h2,
			struct commit_list *ancestors,
			struct tree **result);

#endif
