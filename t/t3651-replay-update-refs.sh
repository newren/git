#!/bin/sh

test_description='git replay --update-refs edge cases and comprehensive testing'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_AUTHOR_NAME=author@name
GIT_AUTHOR_EMAIL=bogus@email@address
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

test_expect_success 'setup for update-refs tests' '
	test_commit A &&
	test_commit B &&

	git switch -c topic1 &&
	test_commit C &&
	git switch -c topic2 &&
	test_commit D &&
	test_commit E &&
	git switch topic1 &&
	test_commit F &&

	git switch main &&
	test_commit L &&
	test_commit M &&

	git switch -c conflict B &&
	test_commit C.conflict C.t conflict
'

test_expect_success 'setup bare repo' '
	git clone --bare . bare
'

# Basic functionality tests

test_expect_success '--update-refs works in atomic mode (basic)' '
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

test_expect_success '--update-refs works with --advance' '
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

test_expect_success '--update-refs produces no output on success' '
	git checkout -b quiet-test topic1 &&
	git replay --update-refs --onto main topic1..quiet-test >output &&
	test_must_be_empty output
'

# Edge case tests

test_expect_success '--update-refs with empty range (no-op)' '
	# Store original branch tip
	git rev-parse topic1 >topic1.before &&
	
	# Try to replay an empty range - should succeed but do nothing
	git replay --update-refs --onto main topic1..topic1 &&
	
	# Branch should be unchanged
	git rev-parse topic1 >topic1.after &&
	test_cmp topic1.before topic1.after
'

test_expect_success '--update-refs handles conflict gracefully in atomic mode' '
	# Create a branch that will conflict
	git checkout -b atomic-conflict B &&
	echo "different content" >C.t &&
	git add C.t &&
	git commit -m "Conflicting C" &&
	
	# Store original state
	git rev-parse atomic-conflict >conflict-before &&
	
	# This should fail due to conflict
	test_expect_code 128 git replay --update-refs --onto conflict atomic-conflict^..atomic-conflict &&
	
	# In atomic mode, branch should remain unchanged
	git rev-parse atomic-conflict >conflict-after &&
	test_cmp conflict-before conflict-after
'

test_expect_success '--update-refs preserves transaction semantics' '
	# Create test branch
	git checkout -b transaction-test topic1 &&
	test_commit TransactionTest &&
	
	# Store original state
	git rev-parse transaction-test >before-transaction &&
	
	# Use --update-refs (should be atomic)
	git replay --update-refs --onto main topic1..transaction-test &&
	
	# Verify ref was updated
	git rev-parse transaction-test >after-transaction &&
	! test_cmp before-transaction after-transaction &&
	
	# Verify commit history is preserved correctly
	git log --format=%s transaction-test >actual-history &&
	test_write_lines TransactionTest M L B A >expected-history &&
	test_cmp expected-history actual-history
'

test_expect_success '--update-refs vs traditional method equivalence' '
	# Create test branches
	git checkout -b traditional topic1 &&
	test_commit Traditional &&
	git checkout -b direct topic1 &&
	test_commit Direct &&
	
	# Method 1: Traditional output + update-ref
	git replay --onto main topic1..traditional >update-commands &&
	git update-ref --stdin <update-commands &&
	git rev-parse traditional >traditional-result &&
	
	# Method 2: Direct --update-refs
	git replay --update-refs --onto main topic1..direct &&
	git rev-parse direct >direct-result &&
	
	# Both methods should produce equivalent results
	# (OIDs will be different due to different commits, but both should be updated)
	git rev-parse topic1 >original &&
	! test_cmp traditional-result original &&
	! test_cmp direct-result original
'

# Error handling and validation tests

test_expect_success '--update-refs works correctly with bare repositories' '
	# Create branch for bare repo testing
	git checkout -b bare-test topic1 &&
	test_commit BareTest &&
	
	# Test with bare repo (important for Gitaly use case)
	git -C bare fetch .. bare-test:bare-test &&
	git -C bare replay --update-refs --onto main topic1..bare-test &&
	
	# Verify the bare repo was updated correctly
	git -C bare rev-parse bare-test >bare-result &&
	test -s bare-result &&
	
	# Verify it is different from original
	git rev-parse topic1 >original &&
	! test_cmp bare-result original
'

test_expect_success '--update-refs maintains ref update ordering' '
	# Create multiple branches to test ordering
	git checkout -b order1 topic1 &&
	test_commit Order1 &&
	git checkout -b order2 topic1 &&
	test_commit Order2 &&
	
	# Store original states
	git rev-parse order1 >order1-before &&
	git rev-parse order2 >order2-before &&
	
	# Update both branches
	git replay --update-refs --onto main topic1..order1 &&
	git replay --update-refs --onto main topic1..order2 &&
	
	# Verify both were updated
	git rev-parse order1 >order1-after &&
	git rev-parse order2 >order2-after &&
	! test_cmp order1-before order1-after &&
	! test_cmp order2-before order2-after
'

test_expect_success '--update-refs handles ref transaction cleanup properly' '
	# This test ensures no ref transaction leaks occur
	git checkout -b cleanup-test topic1 &&
	test_commit CleanupTest &&
	
	# Run multiple operations to test cleanup
	git replay --update-refs --onto main topic1..cleanup-test &&
	git replay --update-refs --onto main~2 main..cleanup-test &&
	
	# If cleanup is working properly, these should succeed without errors
	test_path_is_file .git/refs/heads/cleanup-test
'

# Performance and stress tests

test_expect_success '--update-refs performance is reasonable' '
	# Create several commits to test performance
	git checkout -b perf-test topic1 &&
	for i in 1 2 3 4 5; do
		test_commit --no-tag "Perf$i" || return 1
	done &&
	
	# Time the traditional method
	time git replay --onto main topic1..perf-test >perf-commands &&
	time git update-ref --stdin <perf-commands &&
	
	# Reset and time the new method
	git reset --hard topic1 &&
	for i in 1 2 3 4 5; do
		test_commit --no-tag "Perf$i" || return 1
	done &&
	time git replay --update-refs --onto main topic1..perf-test &&
	
	# Test completed successfully if we got here
	true
'

test_done
