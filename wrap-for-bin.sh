#!/bin/sh

# wrap-for-bin.sh: Template for git executable wrapper scripts
# to run test suite against sandbox, but with only bindir-installed
# executables in PATH.  The Makefile copies this into various
# files in bin-wrappers, substituting
# @@BUILD_DIR@@ and @@PROG@@.

GIT_EXEC_PATH='@@BUILD_DIR@@'
if test -n "$NO_SET_GIT_TEMPLATE_DIR"
then
	unset GIT_TEMPLATE_DIR
else
	GIT_TEMPLATE_DIR='@@BUILD_DIR@@/templates/blt'
	export GIT_TEMPLATE_DIR
fi
GITPERLLIB='@@BUILD_DIR@@/perl/build/lib'"${GITPERLLIB:+:$GITPERLLIB}"
GIT_TEXTDOMAINDIR='@@BUILD_DIR@@/po/build/locale'
PATH='@@BUILD_DIR@@/bin-wrappers:'"$PATH"

export GIT_EXEC_PATH GITPERLLIB PATH GIT_TEXTDOMAINDIR

VALGRIND_RESULTS="valgrind-results.txt"
if [[ "$1" == "rev-list" || "$1" == "log" ]]; then
    if [[ -z "$GIT_DEBUGGER" ]]; then
	rm -f $VALGRIND_RESULTS
	GIT_DEBUGGER="valgrind --track-origins=yes --leak-check=yes --log-file=$VALGRIND_RESULTS"
    fi
fi

case "$GIT_DEBUGGER" in
'')
	exec "${GIT_EXEC_PATH}/@@PROG@@" "$@"
	;;
1)
	unset GIT_DEBUGGER
	exec gdb --args "${GIT_EXEC_PATH}/@@PROG@@" "$@"
	;;
*)
	GIT_DEBUGGER_ARGS="$GIT_DEBUGGER"
	unset GIT_DEBUGGER
	${GIT_DEBUGGER_ARGS} "${GIT_EXEC_PATH}/@@PROG@@" "$@"
	ret=$?
	if test $? -eq 0; then
	    grep -q "ERROR SUMMARY: 0 errors from 0 contexts" $VALGRIND_RESULTS
	    ret=$?
	    if test $ret -ne 0; then
		cat $VALGRIND_RESULTS >&2
	    fi
	fi
	exit $ret
	;;
esac
