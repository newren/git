#!/bin/sh

test_description='test oidmap'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# This purposefully is very similar to t0011-hashmap.sh

test_oidmap () {
	echo "$1" | test-tool oidmap $3 >actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}


test_expect_success 'setup' '

	test_commit one &&
	test_commit two &&
	test_commit three &&
	test_commit four

'

test_expect_success 'put' '

test_oidmap "put one 1
put two 2
put invalidOid 4
put three 3" "NULL
NULL
Unknown oid: invalidOid
NULL"

'

test_expect_success 'replace' '

test_oidmap "put one 1
put two 2
put three 3
put invalidOid 4
put two deux
put one un" "NULL
NULL
NULL
Unknown oid: invalidOid
2
1"

'

test_expect_success 'get' '

test_oidmap "put one 1
put two 2
put three 3
get two
get four
get invalidOid
get one" "NULL
NULL
NULL
2
NULL
Unknown oid: invalidOid
1"

'

test_expect_success 'remove' '

test_oidmap "put one 1
put two 2
put three 3
remove one
remove two
remove invalidOid
remove four" "NULL
NULL
NULL
1
2
Unknown oid: invalidOid
NULL"

'

test_expect_success 'iterate' '
	test-tool oidmap >actual.raw <<-\EOF &&
	put one 1
	put two 2
	put three 3
	iterate
	EOF

	# sort "expect" too so we do not rely on the order of particular oids
	sort >expect <<-EOF &&
	NULL
	NULL
	NULL
	$(git rev-parse one) 1
	$(git rev-parse two) 2
	$(git rev-parse three) 3
	EOF

	sort <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'put_field & get_field' '

	test-tool oidmap >actual <<-\EOF &&
	put_field one 1
	put_field two 2
	put_field one eins
	put_field invalidOid 4
	put_field three 3
	get_field three
	get_field one
	get_field invalidOid
	get_field two
	EOF

	cat >expect <<-EOF &&
	NULL
	NULL
	1
	Unknown oid: invalidOid
	NULL
	3
	eins
	Unknown oid: invalidOid
	2
	EOF

	test_cmp expect actual
'

test_done
