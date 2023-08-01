struct Repository {}
struct DiffFilespec {
    cnt_data: Option<Box<spanhash_top>>,
}
struct SpanHash {
    cnt: u32,
    hashval: u32,
}
struct SpanHashTop {
    data: Vec<SpanHash>,
}

#[no_mangle]
pub extern "C"
fn diffcore_count_changes(
    r: &Repository,
    src: &mut DiffFilespec,
    dst: &mut DiffFilespec,
) -> (u32, u32) {
    let (mut src_count, mut dst_count) = (None, None);

    if src.cnt_data.is_none() {
        src.cnt_data = Some(Box::new(hash_chars(r, src)));
    }
    src_count = src.cnt_data.as_mut();

    if dst.cnt_data.is_none() {
        dst.cnt_data = Some(Box::new(hash_chars(r, dst)));
    }
    dst_count = dst.cnt_data.as_mut();

    let (mut sc, mut la) = (0, 0);
    let (mut s_iter, mut d_iter) = (src_count.unwrap().data.iter(), dst_count.unwrap().data.iter());

    let mut s = s_iter.next();
    let mut d = d_iter.next();

    while let Some(s_val) = s {
        if s_val.cnt == 0 {
            break;
        }
        while let Some(d_val) = d {
            if d_val.cnt == 0 || d_val.hashval >= s_val.hashval {
                break;
            }
            la += d_val.cnt;
            d = d_iter.next();
        }

        let (src_cnt, mut dst_cnt) = (s_val.cnt, 0);
        if let Some(d_val) = d {
            if d_val.hashval == s_val.hashval {
                dst_cnt = d_val.cnt;
                d = d_iter.next();
            }
        }

        if src_cnt < dst_cnt {
            la += dst_cnt - src_cnt;
            sc += src_cnt;
        } else {
            sc += dst_cnt;
        }
        s = s_iter.next();
    }

    while let Some(d_val) = d {
        la += d_val.cnt;
        d = d_iter.next();
    }

    (sc, la)
}