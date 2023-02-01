#!/bin/bash

FLAGS="-DUSE_THE_INDEX_COMPATIBILITY_MACROS -DHAVE_FSMONITOR_DAEMON_BACKEND -Wmissing-prototypes"
for f in $(grep WARNINGS RESULTS | awk {print\$2}); do
    echo "======================================================================"
    echo "=== $f ==="
    echo "======================================================================"
    sed -i '/#include "cache.h"/d' $f
	cat >temp.c <<-EOF &&
	#include "git-compat-util.h"
	#include "$f"
	int main() {}
	EOF

    gcc -c $FLAGS -I. temp.c
    git checkout -q HEAD $f
done
