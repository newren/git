#!/bin/sh

test_description='ort capability rebase tests'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit base &&

	git switch -c otherbase base &&
	>other-file &&
	git add other-file &&
	git commit -m "other file" &&
	
	git switch -c topic base &&
	test_write_lines 1 2 3 4 5 >numbers &&
	git add numbers &&
	git commit -m "add file" numbers &&

	test_write_lines 6 7 8 >>numbers &&
	git commit -am "more numbers" 
'

test_expect_success 'avoid wasted effort' '
	git switch topic &&
	test-tool chmtime =0 numbers &&
	test-tool chmtime =0 base.t &&
	test_path_is_missing other-file &&

	GIT_DEBUGGER="strace -e trace=%file,%process -o rebase.strace" git rebase otherbase &&

	! grep clone rebase.strace &&
	! grep .git/rebase-merge rebase.strace &&

	test_write_lines 0 0 >expect &&
	test-tool chmtime --get numbers base.t >actual &&
	test_cmp expect actual &&

	test 0 != $(test-tool chmtime --get other-file)
'


test_done
