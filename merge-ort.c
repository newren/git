/*
 * "Ostensibly Recursive's Twin" merge strategy, or "ort" for short.  Meant
 * as a drop-in replacement for the "recursive" merge strategy, allowing one
 * to replace
 *
 *   git merge [-s recursive]
 *
 * with
 *
 *   git merge -s ort
 *
 * Note: git's parser allows the space between '-s' and its argument to be
 * missing.  (Should I have backronymed "ham", "alsa", "kip", "nap, "alvo",
 * "cale", "peedy", or "ins" instead of "ort"?)
 */

#include "cache.h"
#include "merge-ort.h"

#include "alloc.h"
#include "cache-tree.h"
#include "commit.h"
#include "commit-reach.h"
#include "diff.h"
#include "diffcore.h"
#include "object-store.h"
#include "unpack-trees.h"

struct merge_options_internal {
	int call_depth;
	int needed_rename_limit;
	struct hashmap current_file_dir_set;
	struct string_list df_conflict_file_set;
	struct unpack_trees_options unpack_opts;
	struct index_state orig_index;
};

/***** Copy-paste static functions from merge-recursive.c *****/

/*
 * Yeah, I know this is kind of odd.  But due to my goals:
 *   1) Minimize churn to merge-recursive.c
 *   2) Eventually just delete merge-recursive.c
 * I decided to just copy-paste these for now.  Once we're ready to switch
 * over, we can
 *   #define merge_recursive merge_ort
 *   #define merge_trees merge_ort_nonrecursive
 * and delete merge-recursive.c.
 */

static struct tree *shift_tree_object(struct repository *repo,
				      struct tree *one, struct tree *two,
				      const char *subtree_shift)
{
	struct object_id shifted;

	if (!*subtree_shift) {
		shift_tree(repo, &one->object.oid, &two->object.oid, &shifted, 0);
	} else {
		shift_tree_by(repo, &one->object.oid, &two->object.oid, &shifted,
			      subtree_shift);
	}
	if (oideq(&two->object.oid, &shifted))
		return two;
	return lookup_tree(repo, &shifted);
}

static inline void set_commit_tree(struct commit *c, struct tree *t)
{
	c->maybe_tree = t;
}

static struct commit *make_virtual_commit(struct repository *repo,
					  struct tree *tree,
					  const char *comment)
{
	struct commit *commit = alloc_commit_node(repo);

	set_merge_remote_desc(commit, comment, (struct object *)commit);
	set_commit_tree(commit, tree);
	commit->object.parsed = 1;
	return commit;
}

static int show(struct merge_options *opt, int v)
{
	return (!opt->priv->call_depth && opt->verbosity >= v) ||
		opt->verbosity >= 5;
}

static void flush_output(struct merge_options *opt)
{
	if (opt->buffer_output < 2 && opt->obuf.len) {
		fputs(opt->obuf.buf, stdout);
		strbuf_reset(&opt->obuf);
	}
}

__attribute__((format (printf, 3, 4)))
static void output(struct merge_options *opt, int v, const char *fmt, ...)
{
	va_list ap;

	if (!show(opt, v))
		return;

	strbuf_addchars(&opt->obuf, ' ', opt->priv->call_depth * 2);

	va_start(ap, fmt);
	strbuf_vaddf(&opt->obuf, fmt, ap);
	va_end(ap);

	strbuf_addch(&opt->obuf, '\n');
	if (!opt->buffer_output)
		flush_output(opt);
}

static int err(struct merge_options *opt, const char *err, ...)
{
	va_list params;

	if (opt->buffer_output < 2)
		flush_output(opt);
	else {
		strbuf_complete(&opt->obuf, '\n');
		strbuf_addstr(&opt->obuf, "error: ");
	}
	va_start(params, err);
	strbuf_vaddf(&opt->obuf, err, params);
	va_end(params);
	if (opt->buffer_output > 1)
		strbuf_addch(&opt->obuf, '\n');
	else {
		error("%s", opt->obuf.buf);
		strbuf_reset(&opt->obuf);
	}

	return -1;
}

static void output_commit_title(struct merge_options *opt, struct commit *commit)
{
	struct merge_remote_desc *desc;

	strbuf_addchars(&opt->obuf, ' ', opt->priv->call_depth * 2);
	desc = merge_remote_util(commit);
	if (desc)
		strbuf_addf(&opt->obuf, "virtual %s\n", desc->name);
	else {
		strbuf_add_unique_abbrev(&opt->obuf, &commit->object.oid,
					 DEFAULT_ABBREV);
		strbuf_addch(&opt->obuf, ' ');
		if (parse_commit(commit) != 0)
			strbuf_addstr(&opt->obuf, _("(bad commit)\n"));
		else {
			const char *title;
			const char *msg = get_commit_buffer(commit, NULL);
			int len = find_commit_subject(msg, &title);
			if (len)
				strbuf_addf(&opt->obuf, "%.*s\n", len, title);
			unuse_commit_buffer(commit, msg);
		}
	}
	flush_output(opt);
}

static inline int merge_detect_rename(struct merge_options *opt)
{
	return (opt->detect_renames >= 0) ? opt->detect_renames : 1;
}

static void init_tree_desc_from_tree(struct tree_desc *desc, struct tree *tree)
{
	parse_tree(tree);
	init_tree_desc(desc, tree->buffer, tree->size);
}

static struct commit_list *reverse_commit_list(struct commit_list *list)
{
	struct commit_list *next = NULL, *current, *backup;
	for (current = list; current; current = backup) {
		backup = current->next;
		current->next = next;
		next = current;
	}
	return next;
}

/***** End copy-paste static functions from merge-recursive.c *****/

static int unpack_trees_start(struct merge_options *opt,
			      struct tree *common,
			      struct tree *head,
			      struct tree *merge)
{
	int rc;
	struct tree_desc t[3];
	struct index_state tmp_index = { NULL };

	memset(&opt->priv->unpack_opts, 0, sizeof(opt->priv->unpack_opts));
	opt->priv->unpack_opts.index_only = 1;
	opt->priv->unpack_opts.merge = 1;
	opt->priv->unpack_opts.head_idx = 2;
	opt->priv->unpack_opts.fn = threeway_merge;
	opt->priv->unpack_opts.src_index = opt->repo->index;
	opt->priv->unpack_opts.dst_index = &tmp_index;
	opt->priv->unpack_opts.aggressive = !merge_detect_rename(opt);
	setup_unpack_trees_porcelain(&opt->priv->unpack_opts, "merge");

	init_tree_desc_from_tree(t+0, common);
	init_tree_desc_from_tree(t+1, head);
	init_tree_desc_from_tree(t+2, merge);

	rc = unpack_trees(3, t, &opt->priv->unpack_opts);
	cache_tree_free(&opt->repo->index->cache_tree);

	opt->priv->orig_index = *opt->repo->index;
	*opt->repo->index = tmp_index;

	return rc;
}

static void unpack_trees_finish(struct merge_options *opt)
{
	discard_index(&opt->priv->orig_index);
	clear_unpack_trees_porcelain(&opt->priv->unpack_opts);
}

static int handle_would_be_overwritten(struct merge_options *opt,
				       const struct cache_entry *old,
				       const struct cache_entry *new)
{
	if (!old)
		return verify_absent(new,
				     ERROR_WOULD_LOSE_UNTRACKED_OVERWRITTEN,
				     &opt->priv->unpack_opts);
	if (!cache_entries_same(old, new))
		return verify_uptodate(old, &opt->priv->unpack_opts);

	/* no untracked or dirty data would be overwritten by this merge */
	return 0;
}

/* Update working tree, which was from opt->priv->orig_index, to the_index */
static int update_working_tree(struct merge_options *opt)
{
	return 0;
}

static const struct cache_entry *get_next(struct index_state *index,
					  int *cur)
{
	if (*cur < 0)
		++*cur;
	else /* Skip unmerged entries with the same name */
		while (++*cur < index->cache_nr &&
		       index->cache[*cur-1]->ce_namelen ==
			 index->cache[*cur]->ce_namelen &&
		       !strncmp(index->cache[*cur-1]->name,
				index->cache[*cur]->name,
				index->cache[*cur]->ce_namelen))
			; /* skip */

	return (*cur < index->cache_nr) ? index->cache[*cur] : NULL;
}

/* Check whether either untracked or dirty files would be overwritten */
static int files_would_be_overwritten(struct merge_options *opt)
{
	int i = -1, j = -1;
	int unsafe = 0;
	const struct cache_entry *old, *new;
	old = get_next(&opt->priv->orig_index, &i);
	new = get_next(opt->repo->index, &j);

	while (old || new) {
		int minlen = old->ce_namelen < new->ce_namelen ?
			     old->ce_namelen : new->ce_namelen;
		int cmp = strncmp(old->name, new->name, minlen);
		if (cmp == 0)
			cmp += new->ce_namelen - old->ce_namelen;
		if (cmp > 0) {
			unsafe |= handle_would_be_overwritten(opt, old, NULL);
			old = get_next(&opt->priv->orig_index, &i);
		} else if (cmp < 0) {
			unsafe |= handle_would_be_overwritten(opt, NULL, new);
			new = get_next(opt->repo->index, &j);
		} else { /* cmp == 0 */
			unsafe |= handle_would_be_overwritten(opt, old, new);
			old = get_next(&opt->priv->orig_index, &i);
			new = get_next(opt->repo->index, &j);
		}
	}

	return unsafe;
}

/*
 * Drop-in replacement for merge_trees_internal().
 * Differences:
 *   1) s/merge_trees_internal/merge_ort_nonrecursive_internal/
 *   2) The handling of unmerged entries has been gutted and replaced with
 *      a BUG() call.  Will be handled later.
 */
static int merge_ort_nonrecursive_internal(struct merge_options *opt,
					   struct tree *head,
					   struct tree *merge,
					   struct tree *merge_base,
					   struct tree **result)
{
	struct index_state *istate = opt->repo->index;
	int code, clean;

	if (opt->priv->call_depth) {
		discard_index(opt->repo->index);
	}

	if (opt->subtree_shift) {
		merge = shift_tree_object(opt->repo, head, merge,
					  opt->subtree_shift);
		merge_base = shift_tree_object(opt->repo, head, merge_base,
					       opt->subtree_shift);
	}

	if (oideq(&merge_base->object.oid, &merge->object.oid)) {
		output(opt, 0, _("Already up to date!"));
		*result = head;
		return 1;
	}

	code = unpack_trees_start(opt, merge_base, head, merge);

	if (code != 0) {
		if (show(opt, 4) || opt->priv->call_depth)
			err(opt, _("merging of trees %s and %s failed"),
			    oid_to_hex(&head->object.oid),
			    oid_to_hex(&merge->object.oid));
		unpack_trees_finish(opt);
		return -1;
	}

	if (!unmerged_index(istate)) {
		clean = 1;
	} else {
		clean = 0;
		BUG("not yet implemented");
	}

	if (opt->priv->call_depth &&
	    !(*result = write_in_core_index_as_tree(opt->repo)))
		return -1;

	if (!opt->priv->call_depth) {
		/*
		 * Update opt->priv->unpack_opts so that functions from
		 * unpack_trees (such as verify_uptodate() which checks
		 * src_index) let us now check for updates in the working
		 * tree against the original index.
		 */
		opt->priv->unpack_opts.index_only = 0;
		opt->priv->unpack_opts.update = 1;
		opt->priv->unpack_opts.src_index = &opt->priv->orig_index;
		if (files_would_be_overwritten(opt)) {
			display_error_msgs(&opt->priv->unpack_opts);
			return -1;
		}
		if (update_working_tree(opt))
			return -1;
	}

	unpack_trees_finish(opt);
	return clean;
}

/*
 * Drop-in replacement for merge_recursive_internal().
 * Currently, a near wholesale copy-paste of merge_recursive_internal(); only
 * the following modifications have been made:
 *   1) s/merge_recursive_internal/merge_ort_internal/
 *   2) s/merge_trees_internal/merge_ort_nonrecursive_internal/
 */
static int merge_ort_internal(struct merge_options *opt,
			      struct commit *h1,
			      struct commit *h2,
			      struct commit_list *merge_bases,
			      struct commit **result)
{
	struct commit_list *iter;
	struct commit *merged_merge_bases;
	struct tree *result_tree;
	int clean;
	const char *ancestor_name;
	struct strbuf merge_base_abbrev = STRBUF_INIT;

	if (show(opt, 4)) {
		output(opt, 4, _("Merging:"));
		output_commit_title(opt, h1);
		output_commit_title(opt, h2);
	}

	if (!merge_bases) {
		merge_bases = get_merge_bases(h1, h2);
		merge_bases = reverse_commit_list(merge_bases);
	}

	if (show(opt, 5)) {
		unsigned cnt = commit_list_count(merge_bases);

		output(opt, 5, Q_("found %u common ancestor:",
				"found %u common ancestors:", cnt), cnt);
		for (iter = merge_bases; iter; iter = iter->next)
			output_commit_title(opt, iter->item);
	}

	merged_merge_bases = pop_commit(&merge_bases);
	if (merged_merge_bases == NULL) {
		/* if there is no common ancestor, use an empty tree */
		struct tree *tree;

		tree = lookup_tree(opt->repo, opt->repo->hash_algo->empty_tree);
		merged_merge_bases = make_virtual_commit(opt->repo, tree,
							 "ancestor");
		ancestor_name = "empty tree";
	} else if (opt->ancestor && !opt->priv->call_depth) {
		ancestor_name = opt->ancestor;
	} else if (merge_bases) {
		ancestor_name = "merged common ancestors";
	} else {
		strbuf_add_unique_abbrev(&merge_base_abbrev,
					 &merged_merge_bases->object.oid,
					 DEFAULT_ABBREV);
		ancestor_name = merge_base_abbrev.buf;
	}

	for (iter = merge_bases; iter; iter = iter->next) {
		const char *saved_b1, *saved_b2;
		opt->priv->call_depth++;
		/*
		 * When the merge fails, the result contains files
		 * with conflict markers. The cleanness flag is
		 * ignored (unless indicating an error), it was never
		 * actually used, as result of merge_trees has always
		 * overwritten it: the committed "conflicts" were
		 * already resolved.
		 */
		saved_b1 = opt->branch1;
		saved_b2 = opt->branch2;
		opt->branch1 = "Temporary merge branch 1";
		opt->branch2 = "Temporary merge branch 2";
		if (merge_ort_internal(opt, merged_merge_bases, iter->item,
				       NULL, &merged_merge_bases) < 0)
			return -1;
		opt->branch1 = saved_b1;
		opt->branch2 = saved_b2;
		opt->priv->call_depth--;

		if (!merged_merge_bases)
			return err(opt, _("merge returned no commit"));
	}

	if (!opt->priv->call_depth && merge_bases != NULL) {
		discard_index(opt->repo->index);
		repo_read_index(opt->repo);
	}

	opt->ancestor = ancestor_name;
	clean = merge_ort_nonrecursive_internal(opt,
				     repo_get_commit_tree(opt->repo, h1),
				     repo_get_commit_tree(opt->repo, h2),
				     repo_get_commit_tree(opt->repo,
							  merged_merge_bases),
				     &result_tree);
	strbuf_release(&merge_base_abbrev);
	opt->ancestor = NULL;  /* avoid accidental re-use of opt->ancestor */
	if (clean < 0) {
		flush_output(opt);
		return clean;
	}

	if (opt->priv->call_depth) {
		*result = make_virtual_commit(opt->repo, result_tree,
					      "merged tree");
		commit_list_insert(h1, &(*result)->parents);
		commit_list_insert(h2, &(*result)->parents->next);
	}
	return clean;
}

static int merge_start(struct merge_options *opt, struct tree *head)
{
	struct strbuf sb = STRBUF_INIT;

	/* Sanity checks on opt */
	assert(opt->repo);

	assert(opt->branch1 && opt->branch2);

	assert(opt->detect_renames >= -1 &&
	       opt->detect_renames <= DIFF_DETECT_COPY);
	assert(opt->detect_directory_renames >= MERGE_DIRECTORY_RENAMES_NONE &&
	       opt->detect_directory_renames <= MERGE_DIRECTORY_RENAMES_TRUE);
	assert(opt->rename_limit >= -1);
	assert(opt->rename_score >= 0 && opt->rename_score <= MAX_SCORE);
	assert(opt->show_rename_progress >= 0 && opt->show_rename_progress <= 1);

	assert(opt->xdl_opts >= 0);
	assert(opt->recursive_variant >= MERGE_VARIANT_NORMAL &&
	       opt->recursive_variant <= MERGE_VARIANT_THEIRS);

	assert(opt->verbosity >= 0 && opt->verbosity <= 5);
	assert(opt->buffer_output <= 2);
	assert(opt->obuf.len == 0);

	assert(opt->priv == NULL);

	/* Sanity check on repo state; index must match head */
	if (repo_index_has_changes(opt->repo, head, &sb)) {
		err(opt, _("Your local changes to the following files would be overwritten by merge:\n  %s"),
		    sb.buf);
		strbuf_release(&sb);
		return -1;
	}

	opt->priv = xcalloc(1, sizeof(*opt->priv));
	string_list_init(&opt->priv->df_conflict_file_set, 1);
	return 0;
}

static void merge_finalize(struct merge_options *opt)
{
	flush_output(opt);
	if (!opt->priv->call_depth && opt->buffer_output < 2)
		strbuf_release(&opt->obuf);
	if (show(opt, 2))
		diff_warn_rename_limit("merge.renamelimit",
				       opt->priv->needed_rename_limit, 0);
	FREE_AND_NULL(opt->priv);
}

int merge_ort_nonrecursive(struct merge_options *opt,
			   struct tree *head,
			   struct tree *merge,
			   struct tree *merge_base)
{
	int clean;
	struct tree *ignored;

	assert(opt->ancestor != NULL);

	if (merge_start(opt, head))
		return -1;
	clean = merge_ort_nonrecursive_internal(opt, head, merge, merge_base,
						&ignored);
	merge_finalize(opt);

	return clean;
}

int merge_ort(struct merge_options *opt,
	      struct commit *h1,
	      struct commit *h2,
	      struct commit_list *merge_bases,
	      struct commit **result)
{
	int clean;

	assert(opt->ancestor == NULL ||
	       !strcmp(opt->ancestor, "constructed merge base"));

	if (merge_start(opt, repo_get_commit_tree(opt->repo, h1)))
		return -1;
	clean = merge_ort_internal(opt, h1, h2, merge_bases, result);
	merge_finalize(opt);

	return clean;
}
