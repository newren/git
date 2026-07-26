#!/bin/sh

test_description='`git pack-aggregate` rolls up small packs and loose objects'

. ./test-lib.sh

# Build N tiny packs in objects/pack/, each containing one distinct
# blob.  Echoes the basenames (without .pack) one per line.
build_n_packs () {
	n=$1 &&
	mkdir -p .git/objects/pack &&
	i=0 &&
	while test $i -lt "$n"
	do
		blob=$(echo "content-$i-$$" | git hash-object -w --stdin) &&
		echo "$blob" |
		git pack-objects --window=0 .git/objects/pack/pack \
			>pack_hash &&
		hash=$(cat pack_hash) &&
		test -n "$hash" &&
		test_path_is_file .git/objects/pack/pack-${hash}.pack &&
		git prune-packed &&
		echo "pack-$hash" &&
		i=$((i + 1)) || return 1
	done
}

# Create N loose objects in the repository.  Echoes the OIDs one per
# line.
build_n_loose () {
	n=$1 &&
	i=0 &&
	while test $i -lt "$n"
	do
		echo "loose-$i-$$" | git hash-object -w --stdin &&
		i=$((i + 1)) || return 1
	done
}

# Build a single pack containing N distinct blobs.  Echoes the pack
# basename (without .pack).
build_big_pack () {
	n=$1 &&
	mkdir -p .git/objects/pack &&
	i=0 &&
	while test $i -lt "$n"
	do
		echo "big-$i-$$" | git hash-object -w --stdin &&
		i=$((i + 1)) || return 1
	done >big_oids &&
	git pack-objects --window=0 .git/objects/pack/pack \
		<big_oids >big_hash &&
	hash=$(cat big_hash) &&
	test -n "$hash" &&
	test_path_is_file .git/objects/pack/pack-${hash}.pack &&
	git prune-packed &&
	echo "pack-$hash"
}

count_packs () {
	ls .git/objects/pack/pack-*.pack 2>/dev/null | wc -l
}

count_baddeltas () {
	ls .git/objects/pack/pack-*.baddeltas 2>/dev/null | wc -l
}

count_loose () {
	find .git/objects \
		-type f \
		-name '[0-9a-f]*' \
		-not -path '.git/objects/pack/*' \
		-not -path '.git/objects/info/*' |
	wc -l
}

test_expect_success 'setup an empty repo' '
	git init repo
'

test_expect_success '--once is required' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		test_must_fail git pack-aggregate 2>err &&
		grep -- "--once is required" err
	)
'

test_expect_success '--once below --min-packs is a no-op for packs' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 3 >/dev/null &&
		test 3 -eq "$(count_packs)" &&
		git pack-aggregate --once --min-loose=1000 &&
		test 3 -eq "$(count_packs)" &&
		test 0 -eq "$(count_baddeltas)"
	)
'

test_expect_success '--once aggregates above --min-packs' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 5 >packs.txt &&
		test 5 -eq "$(count_packs)" &&
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=5 &&
		test 1 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success 'sidecar-marked packs are skipped' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 6 >packs.txt &&
		first=$(head -n 1 packs.txt) &&
		>.git/objects/pack/${first}.keep &&
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=4 &&
		test_path_is_file .git/objects/pack/${first}.pack &&
		test_path_is_file .git/objects/pack/${first}.keep &&
		# 1 kept + 1 aggregate = 2 packs total.
		test 2 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success '--max-objects skips packs above the object-count limit' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		big=$(build_big_pack 20) &&
		build_n_packs 5 >/dev/null &&
		test 6 -eq "$(count_packs)" &&
		# The big pack (20 objects) is above the cap; the five
		# single-object packs are below it and get rolled up.
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-objects=10 &&
		test_path_is_file .git/objects/pack/${big}.pack &&
		# 1 preserved big pack + 1 aggregate = 2 packs total.
		test 2 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success '--max-objects=0 disables the object-count limit' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		big=$(build_big_pack 20) &&
		build_n_packs 5 >/dev/null &&
		test 6 -eq "$(count_packs)" &&
		# With no cap, the big pack is aggregated along with the
		# small ones into a single pack.
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-objects=0 &&
		test_path_is_missing .git/objects/pack/${big}.pack &&
		test 1 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'pack.aggregateMaxObjects supplies the default limit' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		big=$(build_big_pack 20) &&
		build_n_packs 5 >/dev/null &&
		test 6 -eq "$(count_packs)" &&
		git -c pack.aggregateMaxObjects=10 pack-aggregate --once \
			--min-loose=1000 --min-packs=1 &&
		test_path_is_file .git/objects/pack/${big}.pack &&
		test 2 -eq "$(count_packs)" &&
		# An explicit --max-objects still overrides the config.
		big2=$(build_big_pack 20) &&
		git -c pack.aggregateMaxObjects=10 pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-objects=0 &&
		test_path_is_missing .git/objects/pack/${big2}.pack &&
		test 1 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'pack.aggregateMaxObjects rejects negative values' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 5 >/dev/null &&
		test_must_fail git -c pack.aggregateMaxObjects=-1 \
			pack-aggregate --once --min-packs=5 2>err &&
		grep "cannot be negative" err
	)
'

test_expect_success '--max-loose-objects splits the cycle-start backlog' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 10 >/dev/null &&
		git pack-aggregate --once --min-loose=1 --min-packs=1000 \
			--max-loose-objects=4 &&
		test 0 -eq "$(count_loose)" &&
		test 3 -eq "$(count_packs)" &&
		test 3 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success '--min-loose can exceed --max-loose-objects' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 6 >/dev/null &&
		git pack-aggregate --once --min-loose=5 --min-packs=1000 \
			--max-loose-objects=2 &&
		test 0 -eq "$(count_loose)" &&
		test 3 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'multiple loose tranches avoid same-cycle reaggregation' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 10 >/dev/null &&
		build_n_packs 5 >/dev/null &&
		git pack-aggregate --once --min-loose=1 --min-packs=1 \
			--max-loose-objects=4 &&
		# The five original packs are aggregated, while the three
		# loose-rollup packs remain separate until a later cycle.
		test 0 -eq "$(count_loose)" &&
		test 4 -eq "$(count_packs)" &&
		test 4 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success 'loose objects newer than the cycle cutoff are deferred' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 5 >oids &&
		deferred=$(tail -n 1 oids) &&
		deferred_path=.git/objects/$(echo "$deferred" | cut -c1-2)/$(echo "$deferred" | cut -c3-) &&
		test-tool chmtime +60 "$deferred_path" &&
		git pack-aggregate --once --min-loose=1 --min-packs=1000 \
			--max-loose-objects=2 &&
		test 1 -eq "$(count_loose)" &&
		test_path_is_file "$deferred_path" &&
		test-tool chmtime -60 "$deferred_path" &&
		git pack-aggregate --once --min-loose=1 --min-packs=1000 \
			--max-loose-objects=2 &&
		test 0 -eq "$(count_loose)" &&
		git fsck
	)
'

test_expect_success '--max-loose-objects=0 disables tranche splitting' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 10 >/dev/null &&
		git pack-aggregate --once --min-loose=1 --min-packs=1000 \
			--max-loose-objects=0 &&
		test 0 -eq "$(count_loose)" &&
		test 1 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'pack.aggregateMaxLooseObjects supplies the default limit' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 10 >/dev/null &&
		git -c pack.aggregateMaxLooseObjects=4 pack-aggregate --once \
			--min-loose=1 --min-packs=1000 &&
		test 0 -eq "$(count_loose)" &&
		test 3 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'pack.aggregateMaxLooseObjects rejects negative values' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		test_must_fail git -c pack.aggregateMaxLooseObjects=-1 \
			pack-aggregate --once 2>err &&
		grep "cannot be negative" err
	)
'

test_expect_success '--max-packs splits candidates into several output packs' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 10 >/dev/null &&
		test 10 -eq "$(count_packs)" &&
		git cat-file --batch-all-objects --batch-check >before &&
		# 10 candidates, at most 4 per output pack: ceil(10/4)=3
		# batches of sizes 4, 4, 2 => 3 output packs, with no
		# objects lost across the batch boundaries.
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-packs=4 &&
		test 3 -eq "$(count_packs)" &&
		test 3 -eq "$(count_baddeltas)" &&
		git cat-file --batch-all-objects --batch-check >after &&
		test_cmp before after &&
		git fsck
	)
'

test_expect_success '--max-packs above the candidate count yields one pack' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 5 >/dev/null &&
		test 5 -eq "$(count_packs)" &&
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-packs=100 &&
		test 1 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success '--max-packs=0 folds everything into one pack' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 10 >/dev/null &&
		test 10 -eq "$(count_packs)" &&
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-packs=0 &&
		test 1 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'pack.aggregateMaxPacks supplies the default limit' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 10 >/dev/null &&
		test 10 -eq "$(count_packs)" &&
		git -c pack.aggregateMaxPacks=4 pack-aggregate --once \
			--min-loose=1000 --min-packs=1 &&
		test 3 -eq "$(count_packs)" &&
		# An explicit --max-packs=0 still overrides the config,
		# folding the current packs back into a single one.
		build_n_packs 6 >/dev/null &&
		test 9 -eq "$(count_packs)" &&
		git -c pack.aggregateMaxPacks=4 pack-aggregate --once \
			--min-loose=1000 --min-packs=1 --max-packs=0 &&
		test 1 -eq "$(count_packs)" &&
		git fsck
	)
'

test_expect_success 'pack.aggregateMaxPacks rejects negative values' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 5 >/dev/null &&
		test_must_fail git -c pack.aggregateMaxPacks=-1 \
			pack-aggregate --once --min-packs=5 2>err &&
		grep "cannot be negative" err
	)
'

test_expect_success 'aggregate re-rolls up .baddeltas packs' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 5 >/dev/null &&
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=5 &&
		test 1 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		# Now add more small packs and aggregate again.  The
		# previous .baddeltas pack should NOT be protected (we
		# want to keep rolling them up).
		build_n_packs 4 >/dev/null &&
		test 5 -eq "$(count_packs)" &&
		git pack-aggregate --once \
			--min-loose=1000 --min-packs=5 &&
		test 1 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success 'aggregates loose objects above --min-loose' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 5 >/dev/null &&
		test 5 -eq "$(count_loose)" &&
		git pack-aggregate --once \
			--min-loose=1 --min-packs=1000 &&
		test 0 -eq "$(count_loose)" &&
		test 1 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success 'loose then pack aggregation in one cycle' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_loose 5 >/dev/null &&
		build_n_packs 5 >/dev/null &&
		test 5 -eq "$(count_loose)" &&
		test 5 -eq "$(count_packs)" &&
		git pack-aggregate --once \
			--min-loose=1 --min-packs=5 &&
		# Step 1 creates one pack from the loose objects, then
		# step 2 sweeps that pack plus the five existing ones
		# into a single aggregate.
		test 0 -eq "$(count_loose)" &&
		test 1 -eq "$(count_packs)" &&
		test 1 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

# ---- geometric .baddeltas demotion ----

# Helper for the geometric tests: pack a single unique blob into its
# own packfile.  The `tag` parameter must differ between callers so
# that the blob contents do not collide with one another (otherwise
# `git pack-objects --stdin-packs` would see the "rollup" objects
# already present in the kept pack and emit nothing).
make_unique_pack () {
	tag=$1 &&
	blob=$(echo "unique-${tag}-$$" | git hash-object -w --stdin) &&
	echo "$blob" |
	git pack-objects --window=0 .git/objects/pack/pack >/dev/null &&
	git prune-packed
}

test_expect_success 'geometric repack demotes .baddeltas packs into rollup' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		# Build several small packs and consolidate them into
		# one larger pack that would normally sit above the
		# geometric split.
		build_n_packs 8 >/dev/null &&
		git repack -d --geometric=2 &&
		test 1 -eq "$(count_packs)" &&
		big=$(ls .git/objects/pack/pack-*.pack) &&
		# Drop a .baddeltas marker on it (simulating an
		# aggregator output).
		>"${big%.pack}.baddeltas" &&
		test 1 -eq "$(count_baddeltas)" &&
		# Add small packs that are individually far smaller
		# than the big one; ordinarily the big pack would be
		# kept above the geometric split.
		make_unique_pack a &&
		make_unique_pack b &&
		make_unique_pack c &&
		test 4 -eq "$(count_packs)" &&
		# With the .baddeltas demotion, the big pack is rolled
		# up despite being above the natural split, and the
		# resulting pack carries no .baddeltas marker.
		git repack -d --geometric=2 &&
		test 1 -eq "$(count_packs)" &&
		test 0 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_expect_success 'geometric repack leaves non-baddeltas packs above the split alone' '
	test_when_finished "rm -fr work" &&
	cp -R repo work &&
	(
		cd work &&
		build_n_packs 8 >/dev/null &&
		git repack -d --geometric=2 &&
		test 1 -eq "$(count_packs)" &&
		# Add small packs; without any .baddeltas marker the
		# big pack should be preserved above the split.
		make_unique_pack a &&
		make_unique_pack b &&
		make_unique_pack c &&
		test 4 -eq "$(count_packs)" &&
		git repack -d --geometric=2 &&
		# 1 kept big pack + 1 newly-aggregated rollup = 2.
		test 2 -eq "$(count_packs)" &&
		test 0 -eq "$(count_baddeltas)" &&
		git fsck
	)
'

test_done
