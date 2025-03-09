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

#if !defined(XPREPARE_H)
#define XPREPARE_H

void xdl_file_init(struct xdline_t *file);
void xdl_file_prepare(mmfile_t *mf, u64 flags, struct xdline_t *file);
void xdl_file_free(struct xdline_t *file);

int xdl_prepare_env(struct xdline_t *file1, struct xdline_t *file2, usize mph_size, u64 flags, xdfenv_t *xe);

int  xdl_2way_prepare(mmfile_t *mf1, mmfile_t *mf2, u64 flags, struct xd2way *two_way);
void xdl_2way_free(struct xd2way *two_way);
int  xdl_3way_prepare(mmfile_t *mf_base, mmfile_t *mf_side1, mmfile_t *mf_side2, u64 flags, struct xd3way *three_way);
void xdl_3way_free(struct xd3way *three_way);

#endif /* #if !defined(XPREPARE_H) */
