#!/bin/bash

FLAGS="-DUSE_THE_INDEX_COMPATIBILITY_MACROS -DHAVE_FSMONITOR_DAEMON_BACKEND -Wmissing-prototypes"
for f in $(grep WARNINGS RESULTS | awk {print\$2}); do
    echo "======================================================================"
    echo "=== $f ==="
    echo "======================================================================"
    sed -i 's/#include "cache.h"/#include "git-compat-util.h"/' $f
    OBJ_FILE=$(echo $f | sed -e s/\\.c$/\\.o/)
    make DEVELOPER=1 $OBJ_FILE
    git checkout -q HEAD $f
done
