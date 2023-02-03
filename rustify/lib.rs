#![allow(non_camel_case_types)]

use libc::{c_char, c_int, c_uchar, c_uint, c_ulong, c_ushort, c_void};
use c2rust_bitfields::BitfieldStruct;
use std::{{slice,mem::size_of}};
use rustc_hash::FxHashSet;

#[derive(Eq, Hash, PartialEq)]
#[repr(C)]
pub struct spanhash {
    hashval : u32,
    cnt : u32,
}

extern "C" {
    /* Sadly, extern types are experimental */
    /*
    pub type repository;
    */

    fn diff_filespec_is_binary(_: &repository,
                               _: &diff_filespec) -> c_int;
    fn xcalloc(nmemb: size_t, size: size_t) -> *mut libc::c_void;
}

pub type size_t = usize;
const HASHBASE: c_uint = 107927;

#[derive(Copy, Clone)]
#[repr(C)]
pub struct object_id {
    pub hash: [c_uchar; 32],
    pub algo: c_int,
}

#[derive(Clone, BitfieldStruct)]
#[repr(C)]
pub struct diff_filespec {
    pub oid: object_id,
    pub path: *mut c_char,
    pub data: *mut c_void,
    pub cnt_data: *mut c_void,
    pub size: c_ulong,
    pub count: c_int,
    pub rename_used: c_int,
    pub mode: c_ushort,
    #[bitfield(name = "oid_valid", ty = "c_uint", bits = "0..=0")]
    #[bitfield(name = "should_free", ty = "c_uint", bits = "1..=1")]
    #[bitfield(name = "should_munmap", ty = "c_uint", bits = "2..=2")]
    #[bitfield(name = "dirty_submodule", ty = "c_uint", bits = "3..=4")]
    #[bitfield(name = "is_stdin", ty = "c_uint", bits = "5..=5")]
    #[bitfield(name = "has_more_entries", ty = "c_uint", bits = "6..=6")]
    #[bitfield(name = "is_binary", ty = "c_int", bits = "7..=8")]
    pub oid_valid_should_free_should_munmap_dirty_submodule_is_stdin_has_more_entries_is_binary: [u8; 2],
    #[bitfield(padding)]
    pub c2rust_padding: [u8; 4],
    pub driver: *mut userdiff_driver,
}
#[repr(C)]
pub struct OpaqueStruct {
    _data: [u8; 0],
    _marker: core::marker::PhantomData<(*mut u8, core::marker::PhantomPinned)>,
}
type repository = OpaqueStruct;
type userdiff_driver = OpaqueStruct;

#[no_mangle]
pub extern fn spanhash_cmp(a_ : *const c_void, b_ : *const c_void) -> c_int
{
	let a : &spanhash = unsafe { & *(a_ as *const spanhash) };
	let b : &spanhash = unsafe { & *(b_ as *const spanhash) };

	/* A count of zero compares at the end.. */
	if a.cnt == 0 || b.cnt == 0 {
		return b.cnt.cmp(&a.cnt) as c_int;
	}
	a.hashval.cmp(&b.hashval) as c_int
}

#[no_mangle]
pub extern "C" fn hash_chars(
    r: &repository,
    one: &diff_filespec,
) -> *mut spanhash {
    /*
    if one.size == 0 {
        return hash;
    }
    */

    let mut spanset: FxHashSet<spanhash> =
        FxHashSet::with_capacity_and_hasher(one.size as usize/64,
                                            Default::default());
    let is_text = unsafe { !diff_filespec_is_binary(r, one) };

    let buf_slice: &[u8] =
        unsafe { slice::from_raw_parts(&*(one.data as *const u8),
                                       one.size as usize) };
    let mut n = 0;
    let mut accum2: c_uint = 0;
    let mut accum1 = accum2;
    let mut c = buf_slice[0]; // Manually store first item from buffer
    for next in buf_slice.into_iter().skip(1) {
        if is_text != 0 && c == '\r' as u8 && *next == '\n' as u8 {
            c = *next;
            continue;
        }
        (accum1, accum2) = (accum1 << 7 ^ accum2 >> 25,
                            accum2 << 7 ^ accum1 >> 25);
        accum1 = accum1.wrapping_add(c as c_uint);
        n += 1;
        if n < 64 && c != '\n' as u8 {
            c = *next;
            continue;
        }
        let hashval = accum1.wrapping_add(accum2.wrapping_mul(0x61 as c_uint))
                            .wrapping_rem(HASHBASE);
        spanset.insert( spanhash { hashval, cnt: n } );
        n = 0;
        accum2 = 0;
        accum1 = accum2;
        c = *next;
    }
    // Handle last item in buf_slice too.
    {
        (accum1, accum2) = (accum1 << 7 ^ accum2 >> 25,
                            accum2 << 7 ^ accum1 >> 25);
        accum1 = accum1.wrapping_add(c as c_uint);
        n += 1;
        let hashval = accum1.wrapping_add(accum2.wrapping_mul(0x61 as c_uint))
                            .wrapping_rem(HASHBASE);
        spanset.insert( spanhash { hashval, cnt: n } );
    }

    /*
     * I would much rather just take the elements from spanset, turn them into
     * a vec, sort it, and return a pointer to the vec.  But, to avoid having
     * C deallocate memory allocated by C, allocate an array, have the first
     * spanhash store the length, and then sort from 1 to the end.
     */
    let num = 1 + spanset.len();
    let mut newmem = unsafe { xcalloc(1, size_of::<spanhash>() * num)
                              as *mut spanhash
                            };
    let spans = unsafe { slice::from_raw_parts_mut(newmem, num) };
    spans[0].cnt = num as u32;
    let mut j = 1;
    for h in spanset.drain() {
        spans[j] = h;
        j += 1;
    }
    &spans[1..].sort_unstable_by(|a,b| a.hashval.cmp(&b.hashval));
    return spans.as_mut_ptr();
}

#[no_mangle]
fn diffcore_count_changes(r : &repository,
                          src : &mut diff_filespec,
                          dst : &mut diff_filespec,
                          src_count_p : *mut * mut spanhash,
                          dst_count_p : *mut * mut spanhash,
                          src_copied : &mut c_ulong,
                          literal_added : &mut c_ulong)
{
    let src_count : *mut spanhash =
        src_count_p.as_ref().unwrap_or(hash_chars(r, src));
    let dst_count : &[spanhash] =
        dst_count_p.as_ref().unwrap_or(hash_chars(r, src));

    let sc : c_ulong = 0;
    let la : c_ulong = 0;

    let s = 0;
    let d = 0;
    let sn = src_count.len();
    let dn = dst_count.len();
    for s in 0..sn {
        for d in d..dn {

        }
        while d < dn {
            if dst_count[d].hashval >= src_count[s].hashval {
                break;
            }
            la += dst_count[d].cnt;
            d += 1;
        }
        let src_cnt = src_count[s].cnt;
        let dst_cnt = 0;
        if d < dn && dst_count[d].hashval == src_count[s].hashval {
            dst_cnt = dst_count[d].cnt;
            d += 1;
        }
        if src_cnt < dst_cnt {
            la += dst_cnt - src_cnt;
            sc += src_cnt;
        } else {
            sc += dst_cnt;
        }
        s += 1;
    }
    while d < dn {
        la += dst_count[d].cnt;
        d += 1;
    }

    /*
    if (!src_count_p)
        free(src_count);
    if (!dst_count_p)
        free(dst_count);
    */
    *src_copied = sc;
    *literal_added = la;
    return 0;
}
