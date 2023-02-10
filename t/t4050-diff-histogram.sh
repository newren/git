#!/bin/sh

test_description='histogram diff algorithm'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh
. "$TEST_DIRECTORY"/lib-diff-alternative.sh

test_diff_frobnitz "histogram"

test_diff_unique "histogram"

test_expect_success 'line moved to beginning' '
	test_write_lines 1 2 3 4 5 6 7 8 9 >one &&
	test_write_lines 9 1 3 5 7 >two &&
	cat >expect <<-EOF &&
	--- a/one
	+++ b/two
	@@ -1,9 +1,5 @@
	+9
	 1
	-2
	 3
	-4
	 5
	-6
	 7
	-8
	-9
	EOF
	test_must_fail git diff --no-index --histogram one two >tmp &&
	grep -v ^[a-z] tmp >actual &&
	compare_diff_patch expect actual
'

test_done
