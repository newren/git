extern int spanhash_cmp(const void *a_, const void *b_);

struct spanhash {
	unsigned int hashval;
	unsigned int cnt;
};

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
