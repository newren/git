#!/bin/bash

warn() {
    echo "$@" >&2
    git checkout -q HEAD $2
}

FLAGS="-DUSE_THE_INDEX_COMPATIBILITY_MACROS -DHAVE_FSMONITOR_DAEMON_BACKEND -Wmissing-prototypes"
for f in `git grep -l 'include.*"cache.h"' -- '*.h'`; do
	sed -i '/#include "cache.h"/d' $f
	
	if gcc $FLAGS -E -I. $f | grep '"cache.h"' >/dev/null; then
	    warn FAILURE-PREPROCESS $f
	    continue
	fi

	cat >temp.c <<-EOF &&
	#include "git-compat-util.h"
	#include "$f"
	int main() {}
	EOF

	if ! gcc -c $FLAGS -I. temp.c >output 2>&1; then
	    warn FAILURE-COMPILE $f
	    rm output
	    continue
	fi

	if [[ -s output ]]; then
	    echo SUCCESS-WITH-WARNINGS $f
	    git checkout -q HEAD $f
	    rm output
	    continue
	fi
	
	rm output
	echo SUCCESS $f
done
