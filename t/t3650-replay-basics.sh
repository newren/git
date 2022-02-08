#!/bin/sh

test_description='basic git replay tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_AUTHOR_NAME=author@name
GIT_AUTHOR_EMAIL=bogus@email@address
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

test_expect_success 'setup' '
	test_commit A &&
	test_commit B &&

	git switch -c topic1 &&
	test_commit C &&
	git switch -c topic2 &&
	test_commit D &&
	test_commit E &&
	git switch topic1 &&
	test_commit F &&
	git switch -c topic3 &&
	test_commit G &&
	test_commit H &&
	git switch -c topic4 main &&
	test_commit I &&
	test_commit J &&

	git switch -c next main &&
	test_commit K &&
	git merge -m "Merge topic1" topic1 &&
	git merge -m "Merge topic2" topic2 &&
	git merge -m "Merge topic3" topic3 &&
	>evil &&
	git add evil &&
	git commit --amend &&
	git merge -m "Merge topic4" topic4 &&

	git switch main &&
	test_commit L &&
	test_commit M
'

test_expect_success 'using replay to rebase two branches, one on top of other' '
	git replay --onto main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse topic2 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay to perform basic cherry-pick' '
	# The differences between this test and the last one are:
	#   --advance vs --onto
	# 2nd field of result is refs/heads/main vs. refs/heads/topic2
	# 4th field of result is hash for main instead of hash for topic2

	git replay --advance main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/main " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse main >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay to also rebase a contained branch' '
	git replay --contained --onto main main..topic3 >result &&

	test_line_count = 2 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	git log --format=%s $(head -n 1 new-branch-tips) >actual &&
	test_write_lines F C M L B A >expect &&
	test_cmp expect actual &&

	git log --format=%s $(tail -n 1 new-branch-tips) >actual &&
	test_write_lines H G F C M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic1 " >expect &&
	printf "%s " $(head -n 1 new-branch-tips) >>expect &&
	git rev-parse topic1 >>expect &&
	printf "update refs/heads/topic3 " >>expect &&
	printf "%s " $(tail -n 1 new-branch-tips) >>expect &&
	git rev-parse topic3 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --onto main ^topic1 topic2 topic4 >result &&

	test_line_count = 2 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	git log --format=%s $(head -n 1 new-branch-tips) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	git log --format=%s $(tail -n 1 new-branch-tips) >actual &&
	test_write_lines J I M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(head -n 1 new-branch-tips) >>expect &&
	git rev-parse topic2 >>expect &&
	printf "update refs/heads/topic4 " >>expect &&
	printf "%s " $(tail -n 1 new-branch-tips) >>expect &&
	git rev-parse topic4 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --contained --onto main ^main topic2 topic3 topic4 >result &&

	test_line_count = 4 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	>expect &&
	for i in 2 1 3 4
	do
		printf "update refs/heads/topic$i " >>expect &&
		printf "%s " $(grep topic$i result | cut -f 3 -d " ") >>expect &&
		git rev-parse topic$i >>expect
	done &&

	test_cmp expect result &&

	test_write_lines F C M L B A >expect1 &&
	test_write_lines E D C M L B A >expect2 &&
	test_write_lines H G F C M L B A >expect3 &&
	test_write_lines J I M L B A >expect4 &&

	for i in 1 2 3 4
	do
		git log --format=%s $(grep topic$i result | cut -f 3 -d " ") >actual &&
		test_cmp expect$i actual
	done
'

test_expect_success 'using replay to rebase merges too, even evil ones' '
	git replay --contained --onto main ^main next >result &&

	test_line_count = 5 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	>expect &&
	for i in 1 2 3 4
	do
		printf "update refs/heads/topic$i " >>expect &&
		printf "%s " $(grep topic$i result | cut -f 3 -d " ") >>expect &&
		git rev-parse topic$i >>expect
	done &&
	grep next result | cut -f 3 -d " " >new-next &&
	printf "update refs/heads/next " >>expect &&
	printf "%s " $(cat new-next) >>expect &&
	git rev-parse next >>expect &&

	test_cmp expect result &&

	test_write_lines F C M L B A >expect1 &&
	test_write_lines E D C M L B A >expect2 &&
	test_write_lines H G F C M L B A >expect3 &&
	test_write_lines J I M L B A >expect4 &&

	cat <<-\EOF >expect-next &&
	Merge topic4
	J
	I
	Merge topic3
	H
	G
	Merge topic2
	E
	D
	Merge topic1
	F
	C
	K
	M
	L
	B
	A
	EOF

	for i in 1 2 3 4
	do
		git log --format=%s $(grep topic$i result | cut -f 3 -d " ") >actual &&
		test_cmp expect$i actual
	done &&
	git log --format=%s --topo-order $(cat new-next) >actual &&
	test_cmp expect-next actual &&

	cat <<-\EOF >expect &&
	Merge topic4
	Merge topic3
	Merge topic2
	Merge topic1
	EOF
	git log --format=%s --min-parents=2 $(cat new-next) >actual &&
	test_cmp expect actual &&

	git show --remerge-diff --format=%s --name-status $(cat new-next)~1 >actual &&
	q_to_tab <<-\EOF >expect &&
	Merge topic3

	AQevil
	EOF
	test_cmp expect actual
'

test_done
