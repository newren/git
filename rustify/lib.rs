use libc::{c_int, c_uint, c_void};
use c2rust_bitfields::BitfieldStruct;

#[allow(non_camel_case_types)]
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

    fn add_spanhash(top : *mut spanhash_top,
                    hashval: c_uint,
		    cnt: c_int) -> *mut spanhash_top;

    fn diff_filespec_is_binary(_: *const repository,
                               _: *const diff_filespec) -> c_int;
    fn memset(_: *mut c_void,
              _: c_int,
              _: size_t,
             ) -> *mut c_void;
    fn xmalloc(size: size_t) -> *mut c_void;
}

#[allow(non_camel_case_types)]
pub type size_t = usize;
const INITIAL_HASH_SIZE: c_int = 9;
const HASHBASE: c_int = 107927;

#[derive(Copy, Clone)]
#[repr(C)]
pub struct object_id {
    pub hash: [libc::c_uchar; 32],
    pub algo: libc::c_int,
}

#[derive(Copy, Clone, BitfieldStruct)]
#[repr(C)]
pub struct diff_filespec {
    pub oid: object_id,
    pub path: *mut libc::c_char,
    pub data: *mut libc::c_void,
    pub cnt_data: *mut libc::c_void,
    pub size: libc::c_ulong,
    pub count: libc::c_int,
    pub rename_used: libc::c_int,
    pub mode: libc::c_ushort,
    #[bitfield(name = "oid_valid", ty = "libc::c_uint", bits = "0..=0")]
    #[bitfield(name = "should_free", ty = "libc::c_uint", bits = "1..=1")]
    #[bitfield(name = "should_munmap", ty = "libc::c_uint", bits = "2..=2")]
    #[bitfield(name = "dirty_submodule", ty = "libc::c_uint", bits = "3..=4")]
    #[bitfield(name = "is_stdin", ty = "libc::c_uint", bits = "5..=5")]
    #[bitfield(name = "has_more_entries", ty = "libc::c_uint", bits = "6..=6")]
    #[bitfield(name = "is_binary", ty = "libc::c_int", bits = "7..=8")]
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
#[allow(non_camel_case_types)]
type repository = OpaqueStruct;
#[allow(non_camel_case_types)]
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
    let mut buf: *mut libc::c_uchar = (*one).data as *mut libc::c_uchar;
    let mut sz: libc::c_uint = (*one).size as libc::c_uint;
    let is_text = (diff_filespec_is_binary(r, one) == 0) as c_int;
    let i = INITIAL_HASH_SIZE;
    let mut hash = xmalloc(
            ::core::mem::size_of::<c_uint>() +
            ::core::mem::size_of::<c_uint>() +
            ::core::mem::size_of::<spanhash>() as size_t *
                (1 << i) as size_t,
		   ) as *mut spanhash_top;
    (*hash).alloc_log2 = i;
    (*hash).free = ((1 as libc::c_int) << i) * (i - 3 as libc::c_int) / i;
    memset(
        ((*hash).data).as_mut_ptr() as *mut c_void,
        0 as libc::c_int,
        (::core::mem::size_of::<spanhash>() * (1 << i)) as size_t,
    );
    let mut n = 0 as libc::c_int;
    let mut accum2 = 0 as libc::c_int as libc::c_uint;
    let mut accum1 = accum2;
    while sz != 0 {
        let fresh41 = buf;
        buf = buf.offset(1);
        let c = *fresh41 as libc::c_uint;
        let old_1 = accum1;
        sz = sz.wrapping_sub(1);
        if is_text != 0 && c == '\r' as i32 as libc::c_uint && sz != 0
            && *buf as libc::c_int == '\n' as i32
        {
            continue;
        }
        accum1 = accum1 << 7 as libc::c_int ^ accum2 >> 25 as libc::c_int;
        accum2 = accum2 << 7 as libc::c_int ^ old_1 >> 25 as libc::c_int;
        accum1 = accum1.wrapping_add(c);
        n += 1;
        if n < 64 as libc::c_int && c != '\n' as i32 as libc::c_uint {
            continue;
        }
        let hashval = accum1
            .wrapping_add(accum2.wrapping_mul(0x61 as libc::c_int as libc::c_uint))
            .wrapping_rem(HASHBASE as libc::c_uint);
        hash = add_spanhash(hash, hashval, n);
        n = 0 as libc::c_int;
        accum2 = 0 as libc::c_int as libc::c_uint;
        accum1 = accum2;
    }
    if n > 0 {
        let hashval = accum1.wrapping_add(accum2.wrapping_mul(0x61 as c_uint))
                            .wrapping_rem(HASHBASE as libc::c_uint);
        hash = add_spanhash(hash, hashval, n);
    }
    libc::qsort(
        ((*hash).data).as_mut_ptr() as *mut libc::c_void,
        1 << (*hash).alloc_log2,
        ::core::mem::size_of::<spanhash>(),
        Some(
            spanhash_cmp
                as unsafe extern "C" fn(
                    *const libc::c_void,
                    *const libc::c_void,
                ) -> libc::c_int,
        ),
    );
    return hash;
}
