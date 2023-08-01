#[no_mangle]
pub unsafe extern "C"
fn diffcore_count_changes(
    r: *mut repository,
    src: *mut diff_filespec,
    dst: *mut diff_filespec,
    src_copied: *mut c_uint,
    literal_added: *mut c_uint,
) -> i32 {
    let src = &mut *src;
    let dst = &mut *dst;

    let (mut src_count, mut dst_count) = (None, None);
    if src.cnt_data.is_null() {
        src.cnt_data = hash_chars(r, src);
    }
    src_count = Some(&mut *src.cnt_data);

    if dst.cnt_data.is_null() {
        dst.cnt_data = hash_chars(r, dst);
    }
    dst_count = Some(&mut *dst.cnt_data);

    let (mut sc, mut la) = (0, 0);
    let (mut s, mut d) = (src_count.unwrap().data, dst_count.unwrap().data);

    loop {
        if s.cnt == 0 {
            break; // we checked all in src
        }
        while d.cnt != 0 {
            if d.hashval >= s.hashval {
                break;
            }
            la += d.cnt;
            d = d.offset(1);
        }
        let (src_cnt, mut dst_cnt) = (s.cnt, 0);
        if d.cnt != 0 && d.hashval == s.hashval {
            dst_cnt = d.cnt;
            d = d.offset(1);
        }
        if src_cnt < dst_cnt {
            la += dst_cnt - src_cnt;
            sc += src_cnt;
        } else {
            sc += dst_cnt;
        }
        s = s.offset(1);
    }

    while d.cnt != 0 {
        la += d.cnt;
        d = d.offset(1);
    }

    *src_copied = sc;
    *literal_added = la;
    0
}
