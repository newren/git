/*
 *  LibXDiff by Davide Libenzi ( File Differential Library )
 *  Copyright (C) 2003	Davide Libenzi
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/>.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include "xinclude.h"


long xdl_bogosqrt(long n) {
	long i;

	/*
	 * Classical integer square root approximation using shifts.
	 */
	for (i = 1; n > 0; n >>= 2)
		i <<= 1;

	return i;
}


int xdl_emit_diffrec(char const *rec, long size, char const *pre, long psize,
		     xdemitcb_t *ecb) {
	int i = 2;
	mmbuffer_t mb[3];

	mb[0].ptr = (char *) pre;
	mb[0].size = psize;
	mb[1].ptr = (char *) rec;
	mb[1].size = size;
	if (size > 0 && rec[size - 1] != '\n') {
		mb[2].ptr = (char *) "\n\\ No newline at end of file\n";
		mb[2].size = strlen(mb[2].ptr);
		i++;
	}
	if (ecb->out_line(ecb->priv, mb, i) < 0) {

		return -1;
	}

	return 0;
}

void *xdl_mmfile_first(mmfile_t *mmf, long *size)
{
	*size = mmf->size;
	return mmf->ptr;
}


long xdl_mmfile_size(mmfile_t *mmf)
{
	return mmf->size;
}


int xdl_cha_init(chastore_t *cha, long isize, long icount) {

	cha->head = cha->tail = NULL;
	cha->isize = isize;
	cha->nsize = icount * isize;
	cha->ancur = cha->sncur = NULL;
	cha->scurr = 0;

	return 0;
}


void xdl_cha_free(chastore_t *cha) {
	chanode_t *cur, *tmp;

	for (cur = cha->head; (tmp = cur) != NULL;) {
		cur = cur->next;
		xdl_free(tmp);
	}
}


void *xdl_cha_alloc(chastore_t *cha) {
	chanode_t *ancur;
	void *data;

	if (!(ancur = cha->ancur) || ancur->icurr == cha->nsize) {
		if (!(ancur = (chanode_t *) xdl_malloc(sizeof(chanode_t) + cha->nsize))) {

			return NULL;
		}
		ancur->icurr = 0;
		ancur->next = NULL;
		if (cha->tail)
			cha->tail->next = ancur;
		if (!cha->head)
			cha->head = ancur;
		cha->tail = ancur;
		cha->ancur = ancur;
	}

	data = (char *) ancur + sizeof(chanode_t) + ancur->icurr;
	ancur->icurr += cha->isize;

	return data;
}

int xdl_blankline(const char *line, long size, long flags)
{
	long i;

	if (!(flags & XDF_WHITESPACE_FLAGS))
		return (size <= 1);

	for (i = 0; i < size && XDL_ISSPACE(line[i]); i++)
		;

	return (i == size);
}

/*
 * Have we eaten everything on the line, except for an optional
 * CR at the very end?
 */
static int ends_with_optional_cr(const char *l, long s, long i)
{
	int complete = s && l[s-1] == '\n';

	if (complete)
		s--;
	if (s == i)
		return 1;
	/* do not ignore CR at the end of an incomplete line */
	if (complete && s == i + 1 && l[i] == '\r')
		return 1;
	return 0;
}

int xdl_recmatch(const char *l1, long s1, const char *l2, long s2, long flags)
{
	int i1, i2;

	if (s1 == s2 && !memcmp(l1, l2, s1))
		return 1;
	if (!(flags & XDF_WHITESPACE_FLAGS))
		return 0;

	i1 = 0;
	i2 = 0;

	/*
	 * -w matches everything that matches with -b, and -b in turn
	 * matches everything that matches with --ignore-space-at-eol,
	 * which in turn matches everything that matches with --ignore-cr-at-eol.
	 *
	 * Each flavor of ignoring needs different logic to skip whitespaces
	 * while we have both sides to compare.
	 */
	if (flags & XDF_IGNORE_WHITESPACE) {
		goto skip_ws;
		while (i1 < s1 && i2 < s2) {
			if (l1[i1++] != l2[i2++])
				return 0;
		skip_ws:
			while (i1 < s1 && XDL_ISSPACE(l1[i1]))
				i1++;
			while (i2 < s2 && XDL_ISSPACE(l2[i2]))
				i2++;
		}
	} else if (flags & XDF_IGNORE_WHITESPACE_CHANGE) {
		while (i1 < s1 && i2 < s2) {
			if (XDL_ISSPACE(l1[i1]) && XDL_ISSPACE(l2[i2])) {
				/* Skip matching spaces and try again */
				while (i1 < s1 && XDL_ISSPACE(l1[i1]))
					i1++;
				while (i2 < s2 && XDL_ISSPACE(l2[i2]))
					i2++;
				continue;
			}
			if (l1[i1++] != l2[i2++])
				return 0;
		}
	} else if (flags & XDF_IGNORE_WHITESPACE_AT_EOL) {
		while (i1 < s1 && i2 < s2 && l1[i1] == l2[i2]) {
			i1++;
			i2++;
		}
	} else if (flags & XDF_IGNORE_CR_AT_EOL) {
		/* Find the first difference and see how the line ends */
		while (i1 < s1 && i2 < s2 && l1[i1] == l2[i2]) {
			i1++;
			i2++;
		}
		return (ends_with_optional_cr(l1, s1, i1) &&
			ends_with_optional_cr(l2, s2, i2));
	}

	/*
	 * After running out of one side, the remaining side must have
	 * nothing but whitespace for the lines to match.  Note that
	 * ignore-whitespace-at-eol case may break out of the loop
	 * while there still are characters remaining on both lines.
	 */
	if (i1 < s1) {
		while (i1 < s1 && XDL_ISSPACE(l1[i1]))
			i1++;
		if (s1 != i1)
			return 0;
	}
	if (i2 < s2) {
		while (i2 < s2 && XDL_ISSPACE(l2[i2]))
			i2++;
		return (s2 == i2);
	}
	return 1;
}

static unsigned long xdl_hash_record_with_whitespace(char const **data,
		char const *top, long flags) {
	unsigned long ha = 5381;
	char const *ptr = *data;
	int cr_at_eol_only = (flags & XDF_WHITESPACE_FLAGS) == XDF_IGNORE_CR_AT_EOL;

	for (; ptr < top && *ptr != '\n'; ptr++) {
		if (cr_at_eol_only) {
			/* do not ignore CR at the end of an incomplete line */
			if (*ptr == '\r' &&
			    (ptr + 1 < top && ptr[1] == '\n'))
				continue;
		}
		else if (XDL_ISSPACE(*ptr)) {
			const char *ptr2 = ptr;
			int at_eol;
			while (ptr + 1 < top && XDL_ISSPACE(ptr[1])
					&& ptr[1] != '\n')
				ptr++;
			at_eol = (top <= ptr + 1 || ptr[1] == '\n');
			if (flags & XDF_IGNORE_WHITESPACE)
				; /* already handled */
			else if (flags & XDF_IGNORE_WHITESPACE_CHANGE
				 && !at_eol) {
				ha += (ha << 5);
				ha ^= (unsigned long) ' ';
			}
			else if (flags & XDF_IGNORE_WHITESPACE_AT_EOL
				 && !at_eol) {
				while (ptr2 != ptr + 1) {
					ha += (ha << 5);
					ha ^= (unsigned long) *ptr2;
					ptr2++;
				}
			}
			continue;
		}
		ha += (ha << 5);
		ha ^= (unsigned long) *ptr;
	}
	*data = ptr < top ? ptr + 1: ptr;

	return ha;
}

unsigned long xdl_hash_record(char const **data, char const *top, long flags) {
	unsigned long ha = 5381;
	char const *ptr = *data;

	if (flags & XDF_WHITESPACE_FLAGS)
		return xdl_hash_record_with_whitespace(data, top, flags);

	for (; ptr < top && *ptr != '\n'; ptr++) {
		ha += (ha << 5);
		ha ^= (unsigned long) *ptr;
	}
	*data = ptr < top ? ptr + 1: ptr;

	return ha;
}

unsigned int xdl_hashbits(unsigned int size) {
	unsigned int val = 1, bits = 0;

	for (; val < size && bits < CHAR_BIT * sizeof(unsigned int); val <<= 1, bits++);
	return bits ? bits: 1;
}


int xdl_num_out(char *out, long val) {
	char *ptr, *str = out;
	char buf[32];

	ptr = buf + sizeof(buf) - 1;
	*ptr = '\0';
	if (val < 0) {
		*--ptr = '-';
		val = -val;
	}
	for (; val && ptr > buf; val /= 10)
		*--ptr = "0123456789"[val % 10];
	if (*ptr)
		for (; *ptr; ptr++, str++)
			*str = *ptr;
	else
		*str++ = '0';
	*str = '\0';

	return str - out;
}

static int xdl_format_hunk_hdr(long s1, long c1, long s2, long c2,
			       const char *func, long funclen,
			       xdemitcb_t *ecb) {
	int nb = 0;
	mmbuffer_t mb;
	char buf[128];

	memcpy(buf, "@@ -", 4);
	nb += 4;

	nb += xdl_num_out(buf + nb, c1 ? s1: s1 - 1);

	if (c1 != 1) {
		memcpy(buf + nb, ",", 1);
		nb += 1;

		nb += xdl_num_out(buf + nb, c1);
	}

	memcpy(buf + nb, " +", 2);
	nb += 2;

	nb += xdl_num_out(buf + nb, c2 ? s2: s2 - 1);

	if (c2 != 1) {
		memcpy(buf + nb, ",", 1);
		nb += 1;

		nb += xdl_num_out(buf + nb, c2);
	}

	memcpy(buf + nb, " @@", 3);
	nb += 3;
	if (func && funclen) {
		buf[nb++] = ' ';
		if (funclen > sizeof(buf) - nb - 1)
			funclen = sizeof(buf) - nb - 1;
		memcpy(buf + nb, func, funclen);
		nb += funclen;
	}
	buf[nb++] = '\n';

	mb.ptr = buf;
	mb.size = nb;
	if (ecb->out_line(ecb->priv, &mb, 1) < 0)
		return -1;
	return 0;
}

int xdl_emit_hunk_hdr(long s1, long c1, long s2, long c2,
		      const char *func, long funclen,
		      xdemitcb_t *ecb) {
	if (!ecb->out_hunk)
		return xdl_format_hunk_hdr(s1, c1, s2, c2, func, funclen, ecb);
	if (ecb->out_hunk(ecb->priv,
			  c1 ? s1 : s1 - 1, c1,
			  c2 ? s2 : s2 - 1, c2,
			  func, funclen) < 0)
		return -1;
	return 0;
}

int xdl_fall_back_diff(xdfenv_t *diff_env, xpparam_t const *xpp,
		int line1, int count1, int line2, int count2)
{
	/*
	 * This probably does not work outside Git, since
	 * we have a very simple mmfile structure.
	 *
	 * Note: ideally, we would reuse the prepared environment, but
	 * the libxdiff interface does not (yet) allow for diffing only
	 * ranges of lines instead of the whole files.
	 */
	mmfile_t subfile1, subfile2;
	struct xdline_t file1, file2;
	xdfenv_t env;

	subfile1.ptr = (char *) diff_env->xdf1.record->ptr[line1 - 1].ptr;
	subfile1.size = (char *) diff_env->xdf1.record->ptr[line1 + count1 - 2].ptr +
		diff_env->xdf1.record->ptr[line1 + count1 - 2].size_with_eol - subfile1.ptr;
	subfile2.ptr = (char *) diff_env->xdf2.record->ptr[line2 - 1].ptr;
	subfile2.size = (char *) diff_env->xdf2.record->ptr[line2 + count2 - 2].ptr +
		diff_env->xdf2.record->ptr[line2 + count2 - 2].size_with_eol - subfile2.ptr;

	xdl_file_prepare(&subfile1, xpp->flags, &file1);
	xdl_file_prepare(&subfile2, xpp->flags, &file2);

	if (xdl_do_diff(&file1, &file2, xpp, &env) < 0)
		return -1;

	memcpy(diff_env->xdf1.consider.ptr + SENTINEL + line1 - 1, env.xdf1.consider.ptr + SENTINEL, count1);
	memcpy(diff_env->xdf2.consider.ptr + SENTINEL + line2 - 1, env.xdf2.consider.ptr + SENTINEL, count2);

	xdl_free_env(&env);

	return 0;
}


void xdl_mphb_init(struct xdl_minimal_perfect_hash_builder_t *mphb, usize size, u64 flags) {
	mphb->hbits = xdl_hashbits(size);
	mphb->kv_capacity = size;
	mphb->kv_length = 0;
	mphb->flags = flags;

	XDL_CALLOC_ARRAY(mphb->head, 1 << mphb->hbits);
	XDL_ALLOC_ARRAY(mphb->kv, mphb->kv_capacity);
}

u64 xdl_mphb_hash(struct xdl_minimal_perfect_hash_builder_t *mphb, xrecord_t *key) {
	struct xdl_mphb_node_t *node;
	usize hi;

	u64 line_hash = xdl_line_hash(key->ptr, key->size_no_eol, mphb->flags);
	hi = (long) XDL_HASHLONG(line_hash, mphb->hbits);
	for (node = mphb->head[hi]; node; node = node->next) {
		if (node->line_hash == line_hash &&
			xdl_line_equal(node->ptr, node->size_no_eol, key->ptr, key->size_no_eol, mphb->flags))
			break;
	}

	if (node == NULL) {
		node = &mphb->kv[mphb->kv_length];
		node->ptr = key->ptr;
		node->size_no_eol = key->size_no_eol;
		node->line_hash = line_hash;
		node->value = mphb->kv_length++;
		node->next = mphb->head[hi];
		mphb->head[hi] = node;
	}

	return node->value;
}

usize xdl_mphb_finish(struct xdl_minimal_perfect_hash_builder_t *mphb) {
	usize minimal_perfect_hash_size = mphb->kv_length;
	free(mphb->head);
	free(mphb->kv);
	return minimal_perfect_hash_size;
}

usize xdl_strip_eol(u8 const* ptr, usize size, u64 flags) {
	if (size > 0 && ptr[size - 1] == '\n') {
		size--;
	}
	if ((flags & XDF_IGNORE_CR_AT_EOL) != 0 && size > 0 && ptr[size - 1] == '\r') {
		size--;
	}

	return size;
}

void xdl_linereader_init(struct xlinereader_t *it, u8 const* ptr, usize size) {
	it->cur = ptr;
	it->size = size;
}

bool xdl_linereader_next(struct xlinereader_t *it, u8 const **cur, usize *no_eol, usize *with_eol) {
	if (it->size == 0) {
		return false;
	}

	*cur = it->cur;
	it->cur = memchr(it->cur, '\n', it->size);
	if (it->cur) {
		*no_eol = it->cur - *cur;
		*with_eol = *no_eol + 1;
		it->size -= *with_eol;
		it->cur++;
	} else {
		*no_eol = it->size;
		*with_eol = it->size;
		it->size = 0;
	}

	return true;
}

void xdl_whitespace_iter_init(struct xwhitespaceiter_t* it,
	u8 const* ptr, usize line_size_no_eol, u64 flags
) {
#ifdef DEBUG
	if (it == NULL) {
		BUG("xlineiter_t is null");
	}
	if (ptr == NULL) {
		BUG("xdl_line_iter_init() ptr is null");
	}
#endif
	it->ptr = ptr;
	it->size = line_size_no_eol;
	it->index = 0;
	it->flags = flags;
}

bool xdl_whitespace_iter_next(struct xwhitespaceiter_t* it, u8 const** ptr, usize *run_size) {
	if (it->index >= it->size) {
		*ptr = NULL;
		*run_size = 0;
		return false;
	}

	if ((it->flags & XDF_IGNORE_WHITESPACE_WITHIN) == 0) {
		it->index = it->size;
		*ptr = it->ptr;
		*run_size = it->size;
		return true;
	}

	while (true) {
		usize start = it->index;
		if (it->index == it->size) {
			*ptr = NULL;
			*run_size = 0;
			return false;
		}

		/* return contiguous run of not space bytes */
		while (it->index < it->size) {
			if XDL_ISSPACE(it->ptr[it->index]) {
				break;
			}
			it->index += 1;
		}
		if (it->index > start) {
			*ptr = it->ptr + start;
			*run_size = it->index - start;
			return true;
		}
		/* the current byte had better be a space */
#ifdef DEBUG
		if (!XDL_ISSPACE(it->ptr[it->index])) {
			BUG("xdl_line_iter_next XDL_ISSPACE() is false")
		}
#endif

		for (; it->index < it->size; it->index++) {
			if (!XDL_ISSPACE(it->ptr[it->index])) {
				break;
			}
		}

#ifdef DEBUG
		if (it->index <= start) {
			BUG("XDL_ISSPACE() cannot simultaneously be true and false");
		}
#endif
		if ((it->flags & XDF_IGNORE_WHITESPACE_AT_EOL) != 0
		    && it->index == it->size)
		{
			*ptr = NULL;
			*run_size = 0;
			return false;
		}
		if ((it->flags & XDF_IGNORE_WHITESPACE) != 0) {
			continue;
		}
		if ((it->flags & XDF_IGNORE_WHITESPACE_CHANGE) != 0) {
			const u8 *SINGLE_SPACE = (const u8 *) " ";
			if (it->index == it->size) {
				continue;
			}
			*ptr = SINGLE_SPACE;
			*run_size = 1;
			return true;
		}
		*ptr = it->ptr + start;
		*run_size = it->index - start;
		return true;
	}
}

void xdl_whitespace_iter_assert_done(struct xwhitespaceiter_t* it) {
#ifdef DEBUG
	if (it->index < it->size) {
		BUG("xlineiter_t: didn't consume the whole iterator");
	}
	if (it->index > it->size) {
		BUG("xlineiter_t: index was incremented too much");
	}
#endif
	it->ptr = NULL;
	it->size = 0;
	it->index = 0;
	it->flags = 0;
}

#ifdef WITH_RUST
extern u64 xdl_line_hash(u8 const* ptr, usize line_size_no_eol, u64 flags);
#else
u64 xdl_line_hash(u8 const* ptr, usize line_size_no_eol, u64 flags) {
	if ((flags & XDF_IGNORE_WHITESPACE_WITHIN) == 0) {
		u64 hash = 5381;
		for (usize i = 0; i < line_size_no_eol; i++) {
			hash = hash * 33 ^ (u64) ptr[i];
		}
		return hash;
	} else {
		struct xwhitespaceiter_t it;
		u8 const* run_start;
		usize run_size;

		u64 hash = 5381;

		xdl_whitespace_iter_init(&it, ptr, line_size_no_eol, flags);
		while (xdl_whitespace_iter_next(&it, &run_start, &run_size)) {
			for (usize i = 0; i < run_size; i++) {
				hash = hash * 33 ^ (u64) run_start[i];
			}
		}
		xdl_whitespace_iter_assert_done(&it);

		return hash;
	}
}
#endif

bool xdl_line_equal(u8 const* line1, usize size1, u8 const* line2, usize size2, u64 flags) {
	if ((flags & XDF_IGNORE_WHITESPACE_WITHIN) == 0) {
		if (size1 != size2)
			return false;
		return memcmp(line1, line2, size1) == 0;
	} else {
		struct xwhitespaceiter_t it1, it2;
		u8 const *run_start1, *run_start2;
		usize run_size1, run_size2;
		usize i1, i2;
		bool has_next1, has_next2;

#ifdef DEBUG
		validate_line_arguments(line1, size1, flags);
		validate_line_arguments(line2, size2, flags);
#endif

		xdl_whitespace_iter_init(&it1, line1, size1, flags);
		xdl_whitespace_iter_init(&it2, line2, size2, flags);

		has_next1 = xdl_whitespace_iter_next(&it1, &run_start1, &run_size1);
		has_next2 = xdl_whitespace_iter_next(&it2, &run_start2, &run_size2);

		i1 = 0, i2 = 0;
		while (has_next1 && has_next2) {
			while (i1 < run_size1 && i2 < run_size2) {
				if (run_start1[i1] != run_start2[i2])
					return false;
				i1++, i2++;
			}

			if (i1 == run_size1) {
				i1 = 0;
				has_next1 = xdl_whitespace_iter_next(&it1, &run_start1, &run_size1);
			}

			if (i2 == run_size2) {
				i2 = 0;
				has_next2 = xdl_whitespace_iter_next(&it2, &run_start2, &run_size2);
			}
		}

		/*
		 * check for emtpy runs
		 */
		while (has_next1 && run_size1 == 0) {
			has_next1 = xdl_whitespace_iter_next(&it1, &run_start1, &run_size1);
		}

		while (has_next2 && run_size2 == 0) {
			has_next2 = xdl_whitespace_iter_next(&it2, &run_start2, &run_size2);
		}

		return !has_next1 && !has_next2;
	}
}

bool xdl_record_equal(xrecord_t *lhs, xrecord_t *rhs, u64 flags) {
	return xdl_line_equal(lhs->ptr, lhs->size_no_eol,
		rhs->ptr, rhs->size_no_eol, flags);
}


void* xdl_alloc_grow_helper(void *p, long nr, long *alloc, size_t size)
{
	void *tmp = NULL;
	size_t n = ((LONG_MAX - 16) / 2 >= *alloc) ? 2 * *alloc + 16 : LONG_MAX;
	if (nr > n)
		n = nr;
	if (SIZE_MAX / size >= n)
		tmp = xdl_realloc(p, n * size);
	if (tmp) {
		*alloc = n;
	} else {
		xdl_free(p);
		*alloc = 0;
	}
	return tmp;
}
