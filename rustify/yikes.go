package main

//struct spanhash {
//	unsigned int hashval;
//	unsigned int cnt;
//};
import "C"

//export spanhash_cmp
func spanhash_cmp(a *C.struct_spanhash, b *C.struct_spanhash) C.int {
    if a.cnt == 0 {
        if b.cnt == 0 {
	    return 0;
	}
	return 1;
    }
    if b.cnt == 0 {
        return -1;
    }
    if a.hashval > b.hashval {
        return 1;
    }
    if a.hashval < b.hashval {
        return -1;
    }
    return 0;
}

func main() {}
