use libc::{c_uint, c_void};

#[allow(non_camel_case_types)]

struct spanhash {
	hashval : c_uint,
	cnt : c_uint,
}

#[no_mangle]
pub extern fn spanhash_cmp(a_ : *const c_void, b_ : *const c_void) -> i32
{
	let a : &spanhash = unsafe { & *(a_ as *const spanhash) };
	let b : &spanhash = unsafe { & *(b_ as *const spanhash) };

	/* A count of zero compares at the end.. */
	if (*a).cnt == 0 {
		return if (*b).cnt == 0 { 0 } else { 1 };
	}
	if (*b).cnt == 0 {
		return -1;
	}
	return if (*a).hashval < (*b).hashval { -1 } else {
	    if (*a).hashval > (*b).hashval { 1 } else { 0 }
	};
}
