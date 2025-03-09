/*
 *  LibXDiff by Davide Libenzi ( File Differential Library )
 *  Copyright (C) 2003  Davide Libenzi
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
#include "ivec.h"

#define XDL_KPDIS_RUN 4
#define XDL_MAX_EQLIMIT 1024
#define XDL_SIMSCAN_WINDOW 100


static void xdl_free_ctx(xdfile_t *xdf);
static int xdl_clean_mmatch(char const *dis, long i, long start, long e);

void xdl_file_init(struct xdline_t *file) {
	IVEC_INIT(file->minimal_perfect_hash);
	IVEC_INIT(file->record);
}

void xdl_file_prepare(mmfile_t *mf, u64 flags, struct xdline_t *file) {
	struct xlinereader_t reader;

	xdl_file_init(file);

	rust_ivec_reserve_exact(&file->record, mf->size >> 4);

	xdl_linereader_init(&reader, (u8 const *) mf->ptr, mf->size);
	while (true) {
		xrecord_t *rec;
		if (file->record.length >= file->record.capacity)
			rust_ivec_reserve(&file->record, 1);
		rec = &file->record.ptr[file->record.length++];
		if (!xdl_linereader_next(&reader, &rec->ptr, &rec->size_no_eol, &rec->size_with_eol)) {
			file->record.length--;
			break;
		}
	}

	if ((flags & XDF_IGNORE_CR_AT_EOL) != 0) {
		for (usize i = 0; i < file->record.length; i++) {
			xrecord_t *rec = &file->record.ptr[i];
			if (rec->size_no_eol > 0 && rec->ptr[rec->size_no_eol - 1] == '\r')
				rec->size_no_eol--;
		}
	}

	rust_ivec_reserve_exact(&file->minimal_perfect_hash, file->record.length);
}

void xdl_file_free(struct xdline_t *file) {
	rust_ivec_free(&file->minimal_perfect_hash);
	rust_ivec_free(&file->record);
}

// #ifdef WITH_RUST
// extern int rust_xdl_prepare_ctx(mmfile_t *mf, xdfile_t *xdf, u64 flags);
// #endif
// static int c_xdl_prepare_ctx(mmfile_t *mf, xdfile_t *xdf, u64 flags) {
// 	struct xlinereader_t reader;
//
// 	IVEC_INIT(xdf->file.minimal_perfect_hash);
// 	IVEC_INIT(xdf->file.record);
// 	IVEC_INIT(xdf->rindex);
// 	IVEC_INIT(xdf->consider);
//
// 	xdf->minimal_perfect_hash = &xdf->file.minimal_perfect_hash;
// 	xdf->record = &xdf->file.record;
//
// 	rust_ivec_reserve_exact(xdf->record, mf->size >> 4);
//
// 	xdl_linereader_init(&reader, (u8 const *) mf->ptr, mf->size);
// 	while (true) {
// 		xrecord_t *rec;
// 		if (xdf->record->length >= xdf->record->capacity)
// 			rust_ivec_reserve(xdf->record, 1);
// 		rec = &xdf->record->ptr[xdf->record->length++];
// 		if (!xdl_linereader_next(&reader, &rec->ptr, &rec->size_no_eol, &rec->size_with_eol)) {
// 			xdf->record->length--;
// 			break;
// 		}
// 	}
//
// 	if ((flags & XDF_IGNORE_CR_AT_EOL) != 0) {
// 		for (usize i = 0; i < xdf->record->length; i++) {
// 			xrecord_t *rec = &xdf->record->ptr[i];
// 			if (rec->size_no_eol > 0 && rec->ptr[rec->size_no_eol - 1] == '\r')
// 				rec->size_no_eol--;
// 		}
// 	}
//
// 	xdf->consider.capacity = xdf->consider.length = SENTINEL + xdf->record->length + SENTINEL;
// 	XDL_CALLOC_ARRAY(xdf->consider.ptr, xdf->consider.capacity);
//
// 	rust_ivec_reserve_exact(xdf->minimal_perfect_hash, xdf->record->length);
//
// 	return 0;
// }


static void xdl_free_ctx(xdfile_t *xdf) {
	rust_ivec_free(&xdf->consider);
	rust_ivec_free(&xdf->rindex);
}


void xdl_free_env(xdfenv_t *xe) {
	xdl_free_ctx(&xe->xdf1);
	xdl_free_ctx(&xe->xdf2);
}


static int xdl_clean_mmatch(char const *dis, long i, long start, long end) {
	long r, rdis0, rpdis0, rdis1, rpdis1;

	/*
	 * Limits the window the is examined during the similar-lines
	 * scan. The loops below stops when dis[i - r] == 1 (line that
	 * has no match), but there are corner cases where the loop
	 * proceed all the way to the extremities by causing huge
	 * performance penalties in case of big files.
	 */
	if (i - start > XDL_SIMSCAN_WINDOW)
		start = i - XDL_SIMSCAN_WINDOW;
	if (end - i > XDL_SIMSCAN_WINDOW)
		end = i + XDL_SIMSCAN_WINDOW;

	/*
	 * Scans the lines before 'i' to find a run of lines that either
	 * have no match (dis[j] == 0) or have multiple matches (dis[j] > 1).
	 * Note that we always call this function with dis[i] > 1, so the
	 * current line (i) is already a multimatch line.
	 */
	for (r = 1, rdis0 = 0, rpdis0 = 1; (i - r) >= start; r++) {
		if (!dis[i - r])
			rdis0++;
		else if (dis[i - r] == 2)
			rpdis0++;
		else
			break;
	}
	/*
	 * If the run before the line 'i' found only multimatch lines, we
	 * return 0 and hence we don't make the current line (i) discarded.
	 * We want to discard multimatch lines only when they appear in the
	 * middle of runs with nomatch lines (dis[j] == 0).
	 */
	if (rdis0 == 0)
		return 0;
	for (r = 1, rdis1 = 0, rpdis1 = 1; (i + r) < end; r++) {
		if (!dis[i + r])
			rdis1++;
		else if (dis[i + r] == 2)
			rpdis1++;
		else
			break;
	}
	/*
	 * If the run after the line 'i' found only multimatch lines, we
	 * return 0 and hence we don't make the current line (i) discarded.
	 */
	if (rdis1 == 0)
		return 0;
	rdis1 += rdis0;
	rpdis1 += rpdis0;

	return rpdis1 * XDL_KPDIS_RUN < (rpdis1 + rdis1);
}

/*
 * Try to reduce the problem complexity, discard records that have no
 * matches on the other file. Also, lines that have multiple matches
 * might be potentially discarded if they happear in a run of discardable.
 */
static int xdl_cleanup_records(xdfenv_t *xe) {
	long i, nm, mlim;
	ivec_u8 dis1;
	ivec_u8 dis2;
	ivec_xdloccurrence_t occurrence;
	usize end1 = xe->xdf1.record->length - xe->delta_end;
	usize end2 = xe->xdf2.record->length - xe->delta_end;

	IVEC_INIT(dis1);
	IVEC_INIT(dis2);

	dis1.capacity = dis1.length = xe->xdf1.consider.length;
	XDL_CALLOC_ARRAY(dis1.ptr, dis1.capacity);

	dis2.capacity = dis2.length = xe->xdf2.consider.length;
	XDL_CALLOC_ARRAY(dis2.ptr, dis2.capacity);

	rust_ivec_reserve_exact(&xe->xdf1.rindex, xe->xdf1.record->length);
	rust_ivec_reserve_exact(&xe->xdf2.rindex, xe->xdf2.record->length);

	occurrence.capacity = occurrence.length = xe->minimal_perfect_hash_size;
	XDL_CALLOC_ARRAY(occurrence.ptr, occurrence.capacity);

	rust_ivec_reserve_exact(&xe->xdf1.rindex, xe->xdf1.record->length);
	rust_ivec_reserve_exact(&xe->xdf2.rindex, xe->xdf2.record->length);


	for (usize i = 0; i < xe->xdf1.minimal_perfect_hash->length; i++) {
		u64 mph = xe->xdf1.minimal_perfect_hash->ptr[i];
		occurrence.ptr[mph].file1 += 1;
	}

	for (usize i = 0; i < xe->xdf2.minimal_perfect_hash->length; i++) {
		u64 mph = xe->xdf2.minimal_perfect_hash->ptr[i];
		occurrence.ptr[mph].file2 += 1;
	}

	if ((mlim = xdl_bogosqrt(xe->xdf1.record->length)) > XDL_MAX_EQLIMIT)
		mlim = XDL_MAX_EQLIMIT;
	for (i = xe->delta_start; i < end1; i++) {
		u64 mph = xe->xdf1.minimal_perfect_hash->ptr[i];
		nm = occurrence.ptr[mph].file2;
		dis1.ptr[i] = (nm == 0) ? 0: (nm >= mlim) ? 2: 1;
	}

	if ((mlim = xdl_bogosqrt(xe->xdf2.record->length)) > XDL_MAX_EQLIMIT)
		mlim = XDL_MAX_EQLIMIT;
	for (i = xe->delta_start; i < end2; i++) {
		u64 mph = xe->xdf2.minimal_perfect_hash->ptr[i];
		nm = occurrence.ptr[mph].file1;
		dis2.ptr[i] = (nm == 0) ? 0: (nm >= mlim) ? 2: 1;
	}

	for (i = xe->delta_start; i < end1; i++) {
		if (dis1.ptr[i] == 1 ||
		    (dis1.ptr[i] == 2 && !xdl_clean_mmatch((char const *) dis1.ptr, i, xe->delta_start, end1))) {
			rust_ivec_push(&xe->xdf1.rindex, &i);
		} else
			xe->xdf1.consider.ptr[SENTINEL + i] = YES;
	}

	for (i = xe->delta_start; i < end2; i++) {
		if (dis2.ptr[i] == 1 ||
		    (dis2.ptr[i] == 2 && !xdl_clean_mmatch((char const *) dis2.ptr, i, xe->delta_start, end2))) {
			rust_ivec_push(&xe->xdf2.rindex, &i);
		} else
			xe->xdf2.consider.ptr[SENTINEL + i] = YES;
	}

	rust_ivec_free(&dis1);
	rust_ivec_free(&dis2);

	return 0;
}


/*
 * Early trim initial and terminal matching records.
 */
static void xdl_trim_ends(xdfenv_t *xe) {
	usize lim = XDL_MIN(xe->xdf1.record->length, xe->xdf2.record->length);

	for (usize i = 0; i < lim; i++) {
		u64 mph1 = xe->xdf1.minimal_perfect_hash->ptr[i];
		u64 mph2 = xe->xdf2.minimal_perfect_hash->ptr[i];
		if (mph1 != mph2) {
			xe->delta_start = i;
			break;
		}
	}

	for (usize i = 0; i < lim; i++) {
		u64 mph1 = xe->xdf1.minimal_perfect_hash->ptr[xe->xdf1.minimal_perfect_hash->length - 1 - i];
		u64 mph2 = xe->xdf2.minimal_perfect_hash->ptr[xe->xdf2.minimal_perfect_hash->length - 1 - i];
		if (mph1 != mph2) {
			xe->delta_end = i;
			break;
		}
	}
}


static int xdl_optimize_ctxs(xdfenv_t *xe) {
	xdl_trim_ends(xe);

	if (xdl_cleanup_records(xe) < 0) {

		return -1;
	}

	return 0;
}



static void xdl_prepare_xdfile(struct xdline_t *file, xdfile_t *xdf) {
	xdf->minimal_perfect_hash = &file->minimal_perfect_hash;

	xdf->record = &file->record;

	IVEC_INIT(xdf->consider);
	xdf->consider.capacity = xdf->consider.length = xdf->record->length;
	XDL_CALLOC_ARRAY(xdf->consider.ptr, SENTINEL + xdf->consider.capacity + SENTINEL);

	IVEC_INIT(xdf->rindex);
}


#ifdef WITH_RUST
extern int rust_xdl_prepare_env(mmfile_t *mf1, mmfile_t *mf2, ivec_xdloccurrence_t *occ_ptr, u64 flags, xdfenv_t *xe);
int xdl_prepare_env(mmfile_t *mf1, mmfile_t *mf2, u64 flags, xdfenv_t *xe) {

	bool count_occurrences = (flags & (XDF_PATIENCE_DIFF | XDF_HISTOGRAM_DIFF)) == 0;
	IVEC_INIT(xe->occurrence);

	rust_xdl_prepare_ctx(mf1, &xe->xdf1, flags);
	rust_xdl_prepare_ctx(mf2, &xe->xdf2, flags);

	rust_xdl_construct_mph_and_occurrences(xe, count_occurrences, flags);

	if ((flags & (XDF_PATIENCE_DIFF | XDF_HISTOGRAM_DIFF)) == 0) {
		xdl_optimize_ctxs(xe);
	}

	return 0;
}
#else
int xdl_prepare_env(struct xdline_t *file1, struct xdline_t *file2, usize mph_size, u64 flags, xdfenv_t *xe) {
	xe->delta_start = 0;
	xe->delta_end = 0;

	xdl_prepare_xdfile(file1, &xe->xdf1);
	xdl_prepare_xdfile(file2, &xe->xdf2);
	xe->minimal_perfect_hash_size = mph_size;

	if ((flags & (XDF_PATIENCE_DIFF | XDF_HISTOGRAM_DIFF)) == 0) {
		xdl_optimize_ctxs(xe);
	}

	return 0;
}
#endif

static void mph_ingest(struct xdl_minimal_perfect_hash_builder_t *mphb, ivec_xrecord_t *record, ivec_u64 *mph) {
	for (usize i = 0; i < record->length; i++) {
		u64 v = xdl_mphb_hash(mphb, &record->ptr[i]);
		mph->ptr[mph->length++] = v;
	}
}

int xdl_2way_prepare(mmfile_t *mf1, mmfile_t *mf2, u64 flags, struct xd2way *two_way) {
	struct xdl_minimal_perfect_hash_builder_t mphb;
	usize max_unique_size = 0;

	xdl_file_prepare(mf1, flags, &two_way->file1);
	xdl_file_prepare(mf2, flags, &two_way->file2);

	max_unique_size += two_way->file1.record.length;
	max_unique_size += two_way->file2.record.length;
	xdl_mphb_init(&mphb, max_unique_size, flags);

	mph_ingest(&mphb, &two_way->file1.record, &two_way->file1.minimal_perfect_hash);
	mph_ingest(&mphb, &two_way->file2.record, &two_way->file2.minimal_perfect_hash);
	two_way->minimal_perfect_hash_size = xdl_mphb_finish(&mphb);

	return 0;
}

int xdl_3way_prepare(mmfile_t *mf_base, mmfile_t *mf_side1, mmfile_t *mf_side2, u64 flags, struct xd3way *three_way) {
	struct xdl_minimal_perfect_hash_builder_t mphb;
	usize max_unique_size = 0;

	xdl_file_prepare(mf_base, flags, &three_way->base);
	xdl_file_prepare(mf_side1, flags, &three_way->side1);
	xdl_file_prepare(mf_side2, flags, &three_way->side2);

	max_unique_size += three_way->base.record.length;
	max_unique_size += three_way->side1.record.length;
	max_unique_size += three_way->side2.record.length;
	xdl_mphb_init(&mphb, max_unique_size, flags);

	mph_ingest(&mphb, &three_way->base.record, &three_way->base.minimal_perfect_hash);
	mph_ingest(&mphb, &three_way->side1.record, &three_way->side1.minimal_perfect_hash);
	mph_ingest(&mphb, &three_way->side2.record, &three_way->side2.minimal_perfect_hash);
	three_way->minimal_perfect_hash_size = xdl_mphb_finish(&mphb);

	return 0;
}

