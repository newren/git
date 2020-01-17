#ifndef MERGE_ORT_H
#define MERGE_ORT_H

#include "merge-recursive.h"

/*
 * rename-detecting three-way merge, no recursion
 * replacement for merge_trees()
 */
int merge_ort_nonrecursive(struct merge_options *opt,
			   struct tree *head,
			   struct tree *merge,
			   struct tree *common);

/*
 * rename-detecting three-way merge with recursive ancestor consolidation
 * replacement for merge_recursive()
 */
int merge_ort(struct merge_options *opt,
	      struct commit *h1,
	      struct commit *h2,
	      struct commit_list *ancestors,
	      struct tree **result);

#endif
