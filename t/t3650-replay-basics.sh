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
	test_commit M &&

	git switch -c conflict B &&
	test_commit C.conflict C.t conflict
'

test_expect_success 'setup bare' '
	git clone --bare . bare
'

test_expect_success 'using replay to rebase two branches, one on top of other' '
	git replay --no-update-refs --onto main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse topic2 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay on bare repo to rebase two branches, one on top of other' '
	git -C bare replay --no-update-refs --onto main topic1..topic2 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase with a conflict' '
	test_expect_code 128 git replay --onto topic1 B..conflict
'

test_expect_success 'using replay on bare repo to rebase with a conflict' '
	test_expect_code 128 git -C bare replay --onto topic1 B..conflict
'

test_expect_success 'using replay to perform basic cherry-pick' '
	# The differences between this test and previous ones are:
	#   --advance vs --onto
	# 2nd field of result is refs/heads/main vs. refs/heads/topic2
	# 4th field of result is hash for main instead of hash for topic2

	git replay --no-update-refs --advance main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/main " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse main >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay on bare repo to perform basic cherry-pick' '
	git -C bare replay --no-update-refs --advance main topic1..topic2 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'replay on bare repo fails with both --advance and --onto' '
	test_must_fail git -C bare replay --advance main --onto main topic1..topic2 >result-bare
'

test_expect_success 'replay fails when both --advance and --onto are omitted' '
	test_must_fail git replay topic1..topic2 >result
'

test_expect_success 'using replay to also rebase a contained branch' '
	git replay --no-update-refs --contained --onto main main..topic3 >result &&

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

test_expect_success 'using replay on bare repo to also rebase a contained branch' '
	git -C bare replay --no-update-refs --contained --onto main main..topic3 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --no-update-refs --onto main ^topic1 topic2 topic4 >result &&

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

test_expect_success 'using replay on bare repo to rebase multiple divergent branches, including contained ones' '
	git -C bare replay --no-update-refs --contained --onto main ^main topic2 topic3 topic4 >result &&

	test_line_count = 4 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	>expect &&
	for i in 2 1 3 4
	do
		printf "update refs/heads/topic$i " >>expect &&
		printf "%s " $(grep topic$i result | cut -f 3 -d " ") >>expect &&
		git -C bare rev-parse topic$i >>expect || return 1
	done &&

	test_cmp expect result &&

	test_write_lines F C M L B A >expect1 &&
	test_write_lines E D C M L B A >expect2 &&
	test_write_lines H G F C M L B A >expect3 &&
	test_write_lines J I M L B A >expect4 &&

	for i in 1 2 3 4
	do
		git -C bare log --format=%s $(grep topic$i result | cut -f 3 -d " ") >actual &&
		test_cmp expect$i actual || return 1
	done
'

test_expect_success 'merge.directoryRenames=false' '
	# create a test case that stress-tests the rename caching
	git switch -c rename-onto &&

	mkdir -p to-rename &&
	test_commit to-rename/move &&

	mkdir -p renamed-directory &&
	git mv to-rename/move* renamed-directory/ &&
	test_tick &&
	git commit -m renamed-directory &&

	git switch -c rename-from HEAD^ &&
	test_commit to-rename/add-a-file &&
	echo modified >to-rename/add-a-file.t &&
	test_tick &&
	git commit -m modified to-rename/add-a-file.t &&

	git -c merge.directoryRenames=false replay \
		--onto rename-onto rename-onto..rename-from
'

test_expect_success 'using replay with --update-refs to rebase a branch (atomic mode)' '
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	# Store original branch tip
	git rev-parse topic2 >topic2.old &&
	
	# Use --update-refs to directly update refs with transactions
	git replay --update-refs --onto main topic1..topic2 &&
	
	# Verify the branch was actually updated
	git rev-parse topic2 >topic2.new &&
	! test_cmp topic2.old topic2.new &&
	
	# Verify the history is correct
	git log --format=%s topic2 >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'using replay with --update-refs in advance mode' '
	START=$(git rev-parse main) &&
	test_when_finished "git branch -f main $START" &&

	# Store original main tip
	git rev-parse main >main.old &&
	
	# Use --update-refs with --advance
	git replay --update-refs --advance main topic1..topic2 &&
	
	# Verify main was updated
	git rev-parse main >main.new &&
	! test_cmp main.old main.new &&
	
	# Verify the history is correct  
	git log --format=%s main >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'using replay with --update-refs and --contained' '
	START1=$(git rev-parse topic1) &&
	START3=$(git rev-parse topic3) &&
	test_when_finished "git branch -f topic1 $START1" &&
	test_when_finished "git branch -f topic3 $START3" &&

	# Store original branch tips
	git rev-parse topic1 >topic1.old &&
	git rev-parse topic3 >topic3.old &&
	
	# Use --update-refs with --contained
	git replay --update-refs --contained --onto main main..topic3 &&
	
	# Verify both branches were updated
	git rev-parse topic1 >topic1.new &&
	git rev-parse topic3 >topic3.new &&
	! test_cmp topic1.old topic1.new &&
	! test_cmp topic3.old topic3.new &&
	
	# Reset branches back
	git branch -f topic1 $(cat topic1.old) &&
	git branch -f topic3 $(cat topic3.old)
'

test_expect_success 'replay with --update-refs should not produce output when successful' '
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	git replay --update-refs --onto main topic1..topic2 >output &&
	test_must_be_empty output
'

# Edge cases and comprehensive testing for --update-refs

test_expect_success 'setup for edge case tests' '
	# Create some additional branches for testing
	git checkout -b edge1 main &&
	test_commit Edge1 &&
	git checkout -b edge2 main &&
	test_commit Edge2 &&
	git checkout main
'

test_expect_success '--update-refs with conflicting replay (atomic mode fails completely)' '
	# Create a conflict scenario
	git checkout -b conflict-test main &&
	echo "conflict content" > C.t &&
	git add C.t &&
	git commit -m "Conflicting change" &&
	
	# Store original branch state
	git rev-parse conflict-test >conflict-test.old &&
	
	# This should fail due to conflict, and branch should remain unchanged
	test_expect_code 128 git replay --update-refs --onto topic1 main..conflict-test &&
	
	# Verify branch was not updated (atomic transaction rolled back)
	git rev-parse conflict-test >conflict-test.new &&
	test_cmp conflict-test.old conflict-test.new
'

test_expect_success '--update-refs with no commits to replay (empty transaction)' '
	# Try to replay an empty range
	git rev-parse topic1 >topic1.before &&
	
	# This should succeed but do nothing
	git replay --update-refs --onto main topic1..topic1 &&
	
	# Branch should be unchanged
	git rev-parse topic1 >topic1.after &&
	test_cmp topic1.before topic1.after
'

test_expect_success '--update-refs with multiple branches (atomic success)' '
	START1=$(git rev-parse edge1) &&
	START2=$(git rev-parse edge2) &&
	test_when_finished "git branch -f edge1 $START1" &&
	test_when_finished "git branch -f edge2 $START2" &&

	# Store original states
	git rev-parse edge1 >edge1.old &&
	git rev-parse edge2 >edge2.old &&

	# Replay multiple branches atomically
	git replay --update-refs --contained --onto main main..edge1 &&

	test_tick &&  # Force replayed commits to have new timestamp
	git replay --update-refs --contained --onto main main..edge2 &&
	git replay --update-refs --contained --onto main main..edge2 &&

	# Both should be updated
	git rev-parse edge1 >edge1.new &&
	git rev-parse edge2 >edge2.new &&
	! test_cmp edge1.old edge1.new &&
	! test_cmp edge2.old edge2.new
'

test_expect_success '--update-refs preserves ref transaction semantics' '
	# Create branch for testing
	git checkout -b transaction-test main &&
	test_commit TransactionCommit &&
	
	# Store original state
	git rev-parse transaction-test >before-transaction &&
	
	# Use --update-refs (should be atomic)
	git replay --update-refs --onto main~2 main..transaction-test &&
	
	# Verify ref was updated
	git rev-parse transaction-test >after-transaction &&
	! test_cmp before-transaction after-transaction &&
	
	# Verify commit history is correct
	git log --format=%s transaction-test >actual-history &&
	test_write_lines TransactionCommit B A >expected-history &&
	test_cmp expected-history actual-history
'

test_expect_success '--update-refs with --advance preserves branch history' '
	START=$(git rev-parse main) &&
	test_when_finished "git branch -f main $START" &&

	# Test that --advance with --update-refs works correctly
	git checkout -b advance-test main &&
	test_commit AdvanceCommit &&
	
	# Store original main state
	git rev-parse main >main-before-advance &&
	
	# Use --advance with --update-refs
	git replay --update-refs --advance main main..advance-test &&
	
	# Main should be updated
	git rev-parse main >main-after-advance &&
	! test_cmp main-before-advance main-after-advance &&
	
	# Verify main has the right commits
	git log --format=%s main >main-history &&
	test_write_lines AdvanceCommit M L B A >expected-main &&
	test_cmp expected-main main-history
'

test_expect_success '--update-refs handles ref updates consistently with traditional method' '
	# Create test scenario
	git checkout -b consistency-test main &&
	test_commit --no-tag ConsistencyTest &&
	
	# Method 1: Traditional output piped to update-ref
	git checkout -b trad-test consistency-test &&
	git replay --onto main~2 main..consistency-test >update-commands &&
	git update-ref --stdin <update-commands &&
	git rev-parse consistency-test >traditional-result &&

	# Method 2: Direct --update-refs
	git branch -f consistency-test trad-test &&
	git replay --update-refs --onto main~2 main..consistency-test &&
	git rev-parse consistency-test >direct-result &&
	
	# Results should be identical
	test_cmp traditional-result direct-result
'

test_expect_success '--update-refs with bare repository works correctly' '
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	# Test that --update-refs works in bare repositories
	git -C bare replay --update-refs --onto main topic1..topic2 &&
	
	# Verify the bare repo was updated correctly
	git -C bare log --format=%s topic2 >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'using replay to rebase merges too, even basic evil ones' '
	git replay --no-update-refs --contained --onto main ^main next >result &&

	test_line_count = 5 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	>expect &&
	for i in 1 2 3 4
	do
		printf "update refs/heads/topic$i " >>expect &&
		printf "%s " $(grep topic$i result | cut -f 3 -d " ") >>expect &&
		git rev-parse topic$i >>expect || return 1
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
		test_cmp expect$i actual || return 1
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
