#![allow(non_camel_case_types)]

use libc::{c_char, c_int, c_uchar, c_uint, c_ulong, c_ushort, c_void};
use c2rust_bitfields::BitfieldStruct;
use std::{{slice,mem::size_of}};

#[repr(C)]
pub struct spanhash {
    hashval : u32,
    cnt : u32,
}
#[repr(C)]
pub struct spanhash_top {
    pub alloc_log2: c_int,
    pub free: c_int,
    pub data: [spanhash; 0],
}

extern "C" {
    /* Sadly, extern types are experimental */
    /*
    pub type repository;
    */

    fn add_spanhash(top : &mut spanhash_top,
                    hashval: c_uint,
                    cnt: c_int) -> &mut spanhash_top;

    fn diff_filespec_is_binary(_: &repository,
                               _: &diff_filespec) -> c_int;
    fn memset(_: *mut c_void,
              _: c_int,
              _: size_t,
             ) -> *mut c_void;
    fn xmalloc(size: size_t) -> *mut c_void;
}

pub type size_t = usize;
const INITIAL_HASH_SIZE: c_int = 9;
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
pub unsafe extern "C" fn hash_chars(
    r: &repository,
    one: &diff_filespec,
) -> *mut spanhash_top {
    let mut buf = one.data as *const c_uchar;
    let mut sz = one.size;
    let is_text = !diff_filespec_is_binary(r, one);
    let i = INITIAL_HASH_SIZE;
    let mut hash = &mut *(xmalloc(size_of::<spanhash_top>() +
                                  size_of::<spanhash>() * (1 << i),
                                 ) as *mut spanhash_top);
    hash.alloc_log2 = i;
    hash.free = (1 << i) * (i - 3) / i;
    memset(
        hash.data.as_mut_ptr() as *mut c_void,
        0,
        size_of::<spanhash>() * (1 << i),
    );
    let mut n = 0;
    let mut accum2: c_uint = 0;
    let mut accum1 = accum2;
    while sz != 0 {
        let c = *buf as u8;
        buf = buf.offset(1);
        sz -= 1;
        if is_text != 0 && c == '\r' as u8 && sz != 0 && *buf == '\n' as u8 {
            continue;
        }
        (accum1, accum2) = (accum1 << 7 ^ accum2 >> 25,
                            accum2 << 7 ^ accum1 >> 25);
        accum1 = accum1.wrapping_add(c as c_uint);
        n += 1;
        if n < 64 && c != '\n' as u8 {
            continue;
        }
        let hashval = accum1.wrapping_add(accum2.wrapping_mul(0x61 as c_uint))
                            .wrapping_rem(HASHBASE);
        hash = add_spanhash(hash, hashval, n);
        n = 0;
        accum2 = 0;
        accum1 = accum2;
    }
    if n > 0 {
        let hashval = accum1.wrapping_add(accum2.wrapping_mul(0x61 as c_uint))
                            .wrapping_rem(HASHBASE);
        hash = add_spanhash(hash, hashval, n);
    }
    let hdata = slice::from_raw_parts_mut(hash.data.as_mut_ptr(),
                                          1 << hash.alloc_log2);
    hdata.sort_unstable_by(|a,b| {
                  if a.cnt == 0 || b.cnt == 0 {
                      return b.cnt.cmp(&a.cnt);
                  }
                  a.hashval.cmp(&b.hashval)
                  });
    /*
    libc::qsort(
        hash.data.as_mut_ptr() as *mut c_void,
        1 << hash.alloc_log2,
        size_of::<spanhash>(),
        Some(spanhash_cmp)
    );
    */
    return hash;
}
