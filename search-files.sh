#!/bin/bash

warn() {
    echo "$@" >&2
    git checkout -q HEAD $2
}

FLAGS="-DUSE_THE_INDEX_COMPATIBILITY_MACROS -DHAVE_FSMONITOR_DAEMON_BACKEND -Wmissing-prototypes"
for f in `git grep -l 'include.*"cache.h"' -- '*.c'`; do
	sed -i 's/#include "cache.h"/#include "git-compat-util.h"/' $f
	
	if gcc $FLAGS -E -I. $f 2>/dev/null | grep -e '"cache.h"' -e '/cache.h"' >/dev/null; then
	    warn FAILURE-PREPROCESS $f
	    continue
	fi

	OBJ_FILE=$(echo $f | sed -e s/\\.c$/\\.o/)
	if ! make DEVELOPER=1 $OBJ_FILE >/dev/null 2>&1; then

	    if gcc -c $FLAGS -I. $f >/dev/null 2>&1; then
		echo SUCCESS-WITH-WARNINGS $f
		git checkout -q HEAD $f
		continue
	    fi

	    warn FAILURE-COMPILE $f
	    continue
	fi

	echo SUCCESS $f
done
