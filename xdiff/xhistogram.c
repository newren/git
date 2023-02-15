/*
 * Copyright (C) 2010, Google Inc.
 * and other copyright owners as documented in JGit's IP log.
 *
 * This program and the accompanying materials are made available
 * under the terms of the Eclipse Distribution License v1.0 which
 * accompanies this distribution, is reproduced below, and is
 * available at http://www.eclipse.org/org/documents/edl-v10.php
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 * - Neither the name of the Eclipse Foundation, Inc. nor the
 *   names of its contributors may be used to endorse or promote
 *   products derived from this software without specific prior
 *   written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "xinclude.h"

#define MAX_PTR	UINT_MAX
#define MAX_CNT	UINT_MAX

#define LINE_END(n) (line##n + count##n - 1)
#define LINE_END_PTR(n) (*line##n + *count##n - 1)

struct histindex {
	struct record {
		/* ptr means line number, not pointer; crazy xdiff */
		/* cnt does mean "count" as in "frequency count" */
		unsigned int ptr, cnt;
		struct record *next;
	} **records, /* an occurrence */
	  **line_map; /* map of line to record chain */
	chastore_t rcha; /* repeat count histogram arena, i.e. memory pool */
	unsigned int *next_ptrs;
	unsigned int table_bits,
		     records_size,
		     line_map_size;

	unsigned int max_chain_length,
		     key_shift,
		     ptr_shift;

	unsigned int cnt,
		     has_common;

	xdfenv_t *env;
	xpparam_t const *xpp;
};

struct region {
	unsigned int begin1, end1;
	unsigned int begin2, end2;
	unsigned int weight, num;
	unsigned int rc;
	unsigned int last_ae, last_be;
};

#define LINE_MAP(i, a) (i->line_map[(a) - i->ptr_shift])

#define NEXT_PTR(index, ptr) \
	(index->next_ptrs[(ptr) - index->ptr_shift])

#define CNT(index, ptr) \
	((LINE_MAP(index, ptr))->cnt)

#define REC(env, s, l) \
	(env->xdf##s.recs[l - 1])

static int cmp_recs(xrecord_t *r1, xrecord_t *r2)
{
	return r1->ha == r2->ha;

}

#define CMP(i, s1, l1, s2, l2) \
	(cmp_recs(REC(i->env, s1, l1), REC(i->env, s2, l2)))

#define TABLE_HASH(index, side, line) \
	XDL_HASHLONG((REC(index->env, side, line))->ha, index->table_bits)

static int scanA(struct histindex *index, int line1, int count1)
{
	unsigned int ptr, tbl_idx;
	unsigned int chain_len;
	struct record **rec_chain, *rec;

	for (ptr = LINE_END(1); line1 <= ptr; ptr--) {
		tbl_idx = TABLE_HASH(index, 1, ptr);
		rec_chain = index->records + tbl_idx;
		rec = *rec_chain;

		chain_len = 0;
		while (rec) {
			if (CMP(index, 1, rec->ptr, 1, ptr)) {
				/*
				 * ptr is identical to another element. Insert
				 * it onto the front of the existing element
				 * chain.
				 */
				NEXT_PTR(index, ptr) = rec->ptr;
				rec->ptr = ptr;
				/* cap rec->cnt at MAX_CNT */
				rec->cnt = XDL_MIN(MAX_CNT, rec->cnt + 1);
				LINE_MAP(index, ptr) = rec;
				goto continue_scan;
			}

			rec = rec->next;
			chain_len++;
		}

		if (chain_len == index->max_chain_length)
			return -1;

		/*
		 * This is the first time we have ever seen this particular
		 * element in the sequence. Construct a new chain for it.
		 */
		if (!(rec = xdl_cha_alloc(&index->rcha)))
			return -1;
		rec->ptr = ptr;
		rec->cnt = 1;
		rec->next = *rec_chain;
		*rec_chain = rec;
		LINE_MAP(index, ptr) = rec;

continue_scan:
		; /* no op */
	}

	return 0;
}

static int scanB(struct histindex *index, int line2, int count2)
{
	unsigned int ptr, tbl_idx;
	struct record **rec_chain, *rec;

	/* Order doesn't matter, but scanA went backwards so might as well... */
	for (ptr = LINE_END(2); line2 <= ptr; ptr--) {
		tbl_idx = TABLE_HASH(index, 2, ptr);
		rec_chain = index->records + tbl_idx;
		rec = *rec_chain;

		while (rec) {
			if (CMP(index, 1, rec->ptr, 2, ptr)) {
				/* cap rec->cnt at MAX_CNT */
				rec->cnt = XDL_MIN(MAX_CNT, rec->cnt + 1);
				break;
			}

			rec = rec->next;
		}

		/* not found; ignore and move on to next line */
	}

	return 0;
}

static int try_lcs(struct histindex *index, struct region *lcs,
		   struct region *best, int b_ptr,
		   int line1, int count1, int line2, int count2)
{
	unsigned int b_next = b_ptr + 1;
	struct record *rec = index->records[TABLE_HASH(index, 2, b_ptr)];
	/* as, ae, bs, be: file a start, file a end, file b start, file b end */
	/* rc -- repeat count, as in frequency count */
	unsigned int as, ae, bs, be, np, rc;
	int should_break;

	for (; rec; rec = rec->next) {
		if (rec->cnt > index->cnt) {
			if (!index->has_common)
				index->has_common = CMP(index, 1, rec->ptr, 2, b_ptr);
			continue;
		}

		as = rec->ptr;
		if (!CMP(index, 1, as, 2, b_ptr))
			continue;

		index->has_common = 1;
		for (;;) {
			should_break = 0;
			np = NEXT_PTR(index, as);
			bs = b_ptr;
			ae = as;
			be = bs;
			rc = rec->cnt;

			while (line1 < as && line2 < bs
				&& CMP(index, 1, as - 1, 2, bs - 1)) {
				as--;
				bs--;
				if (1 < rc)
					rc = XDL_MIN(rc, CNT(index, as));
			}
			while (ae < LINE_END(1) && be < LINE_END(2)
				&& CMP(index, 1, ae + 1, 2, be + 1)) {
				ae++;
				be++;
				if (1 < rc)
					rc = XDL_MIN(rc, CNT(index, ae));
			}

			if (b_next <= be)
				b_next = be + 1;
			if (rc > best->rc) {
				/* Ignore this region */
			} else if (best->last_ae < as &&
				   best->last_be < bs &&
				   rc == best->rc) {
				/* Add this region to current weight */
				int new_weight = ae - as + 1;
				best->weight += new_weight;
				best->num += 1;
				best->last_ae = ae;
				best->last_be = be;
			} else {
				/*
				 * Start a new region, but maybe first save
				 * old one if it's the best so far.
				 */
				if (lcs->weight < best->weight ||
				    best->rc < index->cnt) {
					memcpy(lcs, best, sizeof(*best));
					index->cnt = best->rc;
				}
				best->weight = ae - as + 1;
				best->num = 1;
				best->rc = rc;
				best->begin1 = as;
				best->end1 = ae;
				best->begin2 = bs;
				best->end2 = be;
				best->last_ae = ae;
				best->last_be = be;
			}

			if (np == 0)
				break;

			while (np <= ae) {
				np = NEXT_PTR(index, np);
				if (np == 0) {
					should_break = 1;
					break;
				}
			}

			if (should_break)
				break;

			as = np;
		}
	}
	if (lcs->weight < best->weight) {
		memcpy(lcs, best, sizeof(*best));
		index->cnt = best->rc;
	}
	return b_next;
}

static int fall_back_to_classic_diff(xpparam_t const *xpp, xdfenv_t *env,
		int line1, int count1, int line2, int count2)
{
	xpparam_t xpparam;

	memset(&xpparam, 0, sizeof(xpparam));
	xpparam.flags = xpp->flags & ~XDF_DIFF_ALGORITHM_MASK;

	return xdl_fall_back_diff(env, &xpparam,
				  line1, count1, line2, count2);
}

static inline void free_index(struct histindex *index)
{
	xdl_free(index->records);
	xdl_free(index->line_map);
	xdl_free(index->next_ptrs);
	xdl_cha_free(&index->rcha);
}

static int find_lcs(xpparam_t const *xpp, xdfenv_t *env,
		    struct histindex *index, struct region *lcs,
		    int line1, int count1, int line2, int count2)
{
	int b_ptr;
	int ret = -1;
	struct region best = { .weight = UINT_MAX,
			       .rc = UINT_MAX,
			       .last_ae = UINT_MAX,
			       .last_be = UINT_MAX };

	memset(index, 0, sizeof(*index));

	index->env = env;
	index->xpp = xpp;

	index->records = NULL;
	index->line_map = NULL;
	/* in case of early xdl_cha_free() */
	index->rcha.head = NULL;

	index->table_bits = xdl_hashbits(count1);
	index->records_size = 1 << index->table_bits;
	if (!XDL_CALLOC_ARRAY(index->records, index->records_size))
		goto cleanup;

	index->line_map_size = count1;
	if (!XDL_CALLOC_ARRAY(index->line_map, index->line_map_size))
		goto cleanup;

	if (!XDL_CALLOC_ARRAY(index->next_ptrs, index->line_map_size))
		goto cleanup;

	/* lines / 4 + 1 comes from xprepare.c:xdl_prepare_ctx() */
	if (xdl_cha_init(&index->rcha, sizeof(struct record), count1 / 4 + 1) < 0)
		goto cleanup;

	index->ptr_shift = line1;
	index->max_chain_length = 64;

	if (scanA(index, line1, count1))
		goto cleanup;

	if (scanB(index, line2, count2))
		goto cleanup;

	index->cnt = index->max_chain_length + 1;

	for (b_ptr = line2; b_ptr <= LINE_END(2); )
		b_ptr = try_lcs(index, lcs, &best, b_ptr,
				line1, count1, line2, count2);

	if (index->has_common && index->max_chain_length < index->cnt)
		ret = 1;
	else
		ret = 0;

cleanup:
	return ret;
}

static int histogram_diff(xpparam_t const *xpp, xdfenv_t *env,
	int line1, int count1, int line2, int count2)
{
	struct histindex index;
	struct region lcs;
	int lcs_found;
	int result = -1;

	if (count1 <= 0 && count2 <= 0)
		return 0;

	if (LINE_END(1) >= MAX_PTR)
		return -1;

	if (!count1) {
		while(count2--)
			env->xdf2.rchg[line2++ - 1] = 1;
		return 0;
	} else if (!count2) {
		while(count1--)
			env->xdf1.rchg[line1++ - 1] = 1;
		return 0;
	}

	memset(&lcs, 0, sizeof(lcs));
	lcs_found = find_lcs(xpp, env, &index, &lcs, line1, count1, line2, count2);
	if (lcs_found < 0)
		goto out;
	else if (lcs_found)
		result = fall_back_to_classic_diff(xpp, env, line1, count1, line2, count2);
	else {
		if (lcs.num == 0) {
			while (count1--)
				env->xdf1.rchg[line1++ - 1] = 1;
			while (count2--)
				env->xdf2.rchg[line2++ - 1] = 1;
			result = 0;
		} else {
			int num = lcs.num;
			for (int i = 0; ; i++) {
				int b_ptr;
				struct region best = { .rc = UINT_MAX,
						       .last_ae = UINT_MAX,
						       .last_be = UINT_MAX };
				result = histogram_diff(xpp, env,
							line1, lcs.begin1 - line1,
							line2, lcs.begin2 - line2);
				if (result)
					goto out;

				/* Advance line1 & line2 after lcs */
				count1 = LINE_END(1) - lcs.end1;
				line1 = lcs.end1 + 1;
				count2 = LINE_END(2) - lcs.end2;
				line2 = lcs.end2 + 1;
				if (i == num - 1)
					break;

				/* Find the next lcs */
				b_ptr = lcs.end2 + 1;
				memset(&lcs, 0, sizeof(lcs));
				while (!lcs.begin1) {
					b_ptr = try_lcs(&index, &lcs, &best, b_ptr,
							line1, count1, line2, count2);
				}
			}
			result = histogram_diff(xpp, env,
						line1, count1, line2, count2);
		}
	}
out:
	free_index(&index);
	return result;
}

int xdl_do_histogram_diff(xpparam_t const *xpp, xdfenv_t *env)
{
	return histogram_diff(xpp, env,
		env->xdf1.dstart + 1, env->xdf1.dend - env->xdf1.dstart + 1,
		env->xdf2.dstart + 1, env->xdf2.dend - env->xdf2.dstart + 1);
}
