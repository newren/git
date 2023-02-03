#include "git-compat-util.h"
#include "diffcore.h"

extern int spanhash_cmp(const void *a_, const void *b_);
extern struct spanhash_top *hash_chars(struct repository *r,
				       struct diff_filespec *one);
struct spanhash_top *add_spanhash(struct spanhash_top *top,
				  unsigned int hashval, int cnt);

struct spanhash {
	unsigned int hashval;
	unsigned int cnt;
};
struct spanhash_top {
	int alloc_log2;
	int free;
	struct spanhash data[FLEX_ARRAY];
};

/* Wild guess at the initial hash size */
#define INITIAL_HASH_SIZE 9

/* We leave more room in smaller hash but do not let it
 * grow to have unused hole too much.
 */
#define INITIAL_FREE(sz_log2) ((1<<(sz_log2))*(sz_log2-3)/(sz_log2))

/* A prime rather carefully chosen between 2^16..2^17, so that
 * HASHBASE < INITIAL_FREE(17).  We want to keep the maximum hashtable
 * size under the current 2<<17 maximum, which can hold this many
 * different values before overflowing to hashtable of size 2<<18.
 */
#define HASHBASE 107927

int spanhash_cmp(const void *a_, const void *b_)
{
	const struct spanhash *a = a_;
	const struct spanhash *b = b_;

	/* A count of zero compares at the end.. */
	if (!a->cnt)
		return !b->cnt ? 0 : 1;
	if (!b->cnt)
		return -1;
	return a->hashval < b->hashval ? -1 :
		a->hashval > b->hashval ? 1 : 0;
}

struct spanhash_top *hash_chars(struct repository *r,
				struct diff_filespec *one)
{
	int i, n;
	unsigned int accum1, accum2, hashval;
	struct spanhash_top *hash;
	unsigned char *buf = one->data;
	unsigned int sz = one->size;
	int is_text = !diff_filespec_is_binary(r, one);

	i = INITIAL_HASH_SIZE;
	hash = xmalloc(st_add(sizeof(*hash),
			      st_mult(sizeof(struct spanhash), (size_t)1 << i)));
	hash->alloc_log2 = i;
	hash->free = INITIAL_FREE(i);
	memset(hash->data, 0, sizeof(struct spanhash) * ((size_t)1 << i));

	n = 0;
	accum1 = accum2 = 0;
	while (sz) {
		unsigned int c = *buf++;
		unsigned int old_1 = accum1;
		sz--;

		/* Ignore CR in CRLF sequence if text */
		if (is_text && c == '\r' && sz && *buf == '\n')
			continue;

		accum1 = (accum1 << 7) ^ (accum2 >> 25);
		accum2 = (accum2 << 7) ^ (old_1 >> 25);
		accum1 += c;
		if (++n < 64 && c != '\n')
			continue;
		hashval = (accum1 + accum2 * 0x61) % HASHBASE;
		hash = add_spanhash(hash, hashval, n);
		n = 0;
		accum1 = accum2 = 0;
	}
	if (n > 0) {
		hashval = (accum1 + accum2 * 0x61) % HASHBASE;
		hash = add_spanhash(hash, hashval, n);
	}
	QSORT(hash->data, (size_t)1ul << hash->alloc_log2, spanhash_cmp);
	return hash;
}

int diffcore_count_changes(struct repository *r,
			   struct diff_filespec *src,
			   struct diff_filespec *dst,
			   void **src_count_p,
			   void **dst_count_p,
			   unsigned long *src_copied,
			   unsigned long *literal_added)
{
	struct spanhash *s, *d;
	struct spanhash_top *src_count, *dst_count;
	unsigned long sc, la;

	src_count = dst_count = NULL;
	if (src_count_p)
		src_count = *src_count_p;
	if (!src_count) {
		src_count = hash_chars(r, src);
		if (src_count_p)
			*src_count_p = src_count;
	}
	if (dst_count_p)
		dst_count = *dst_count_p;
	if (!dst_count) {
		dst_count = hash_chars(r, dst);
		if (dst_count_p)
			*dst_count_p = dst_count;
	}
	sc = la = 0;

	s = src_count->data;
	d = dst_count->data;
	for (;;) {
		unsigned dst_cnt, src_cnt;
		if (!s->cnt)
			break; /* we checked all in src */
		while (d->cnt) {
			if (d->hashval >= s->hashval)
				break;
			la += d->cnt;
			d++;
		}
		src_cnt = s->cnt;
		dst_cnt = 0;
		if (d->cnt && d->hashval == s->hashval) {
			dst_cnt = d->cnt;
			d++;
		}
		if (src_cnt < dst_cnt) {
			la += dst_cnt - src_cnt;
			sc += src_cnt;
		}
		else
			sc += dst_cnt;
		s++;
	}
	while (d->cnt) {
		la += d->cnt;
		d++;
	}

	if (!src_count_p)
		free(src_count);
	if (!dst_count_p)
		free(dst_count);
	*src_copied = sc;
	*literal_added = la;
	return 0;
}
