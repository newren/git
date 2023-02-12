#!/bin/sh

test_description='comparison of different diff algorithms'

# IMPORTANT NOTE: This file is NOT meant to be a rigid test of correctness
# for each of the different diff algorithms.  They could change over time
# and improve.  I've tried to capture that with expectation filenames
# containing words like "good" or "better" rather than "correct".  However,
# having tests like this helps us avoid accidentally regressing other
# testcases and let us make a conscious decision about tradeoffs rather
# than trying to optimize for a particular issue in isolation.

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

show_diff() {
	git diff --diff-algorithm=$1 HEAD~1 $2 >tmp &&
	grep -v ^index tmp >actual &&
	rm tmp
}

test_expect_success 'setup test files' '
	cat <<-EOF >salutations.c &&
		void bye(void)
		{
		    printf("goodbye\n");
		}
		
		void hello(void)
		{
		    printf("hello\n");
		}
		EOF

	cat <<-EOF >tweak.c &&
		int twiddle(struct point *p1, int factor)
		{
		    if (factor > 0)
		    {
		        p1.y *= factor;
		    }
		
		    return 0;
		}
		
		void add(int a, int b)
		{
		    return a + b;
		}
		EOF

	cat <<-EOF >operations.c &&
		int add(int a, int b)
		{
		    log();
		    return a + b;
		}
		
		int div(int a, int b)
		{
		    if (a == b)
		    {
		        return 1;
		    }
		    log();
		    return a / b;
		    /* TODO: What were the special cases again? */
		}
		
		void declaration(void);
		EOF
		
	git add salutations.c tweak.c operations.c &&
	git commit -m initial &&

	cat <<-EOF >salutations.c &&
		void hello(void)
		{
		    printf("hello\n");
		}
		
		void bye(void)
		{
		    printf("goodbye\n");
		}
		EOF

	cat <<-EOF >tweak.c &&
		int slack(char *msg, int count)
		{
		    if (count)
		    {
		        puts(msg);
		    }
		
		    return 0;
		}
		
		int twiddle(struct point *p1, int factor)
		{
		    if (factor != 0)
		    {
		        p1.x *= factor;
		        p1.y *= factor;
		    }
		
		    return 0;
		}
		EOF

	cat <<-EOF >operations.c &&
		int div(int a, int b)
		{
		    /* TODO: What were the special cases again? */
		    if (a == 0)
		    {
		        return 0xdeadbeef
		    }
		    log();
		    return a / b;
		}
		
		int mul(int a, int b)
		{
		    if (a > 65535 || b > 65535)
		    {
		        return MAX_INT;
		    }
		    log();
		    return a * b;
		}
		
		void declaration(void);
		EOF
	
	git add salutations.c tweak.c operations.c &&
	git commit -m changed
'

test_expect_success 'move one function' '
	cat <<-\EOF >expect-salutations.c-lame &&
		diff --git a/salutations.c b/salutations.c
		--- a/salutations.c
		+++ b/salutations.c
		@@ -1,9 +1,9 @@
		-void bye(void)
		+void hello(void)
		 {
		-    printf("goodbye\n");
		+    printf("hello\n");
		 }
		 
		-void hello(void)
		+void bye(void)
		 {
		-    printf("hello\n");
		+    printf("goodbye\n");
		 }
		EOF

	cat <<-\EOF >expect-salutations.c-better &&
		diff --git a/salutations.c b/salutations.c
		--- a/salutations.c
		+++ b/salutations.c
		@@ -1,9 +1,9 @@
		-void bye(void)
		-{
		-    printf("goodbye\n");
		-}
		-
		 void hello(void)
		 {
		     printf("hello\n");
		 }
		+
		+void bye(void)
		+{
		+    printf("goodbye\n");
		+}
		EOF

	show_diff myers salutations.c >actual &&
	test_cmp expect-salutations.c-lame actual &&

	show_diff minimal salutations.c >actual &&
	test_cmp expect-salutations.c-lame actual &&

	show_diff patience salutations.c >actual &&
	test_cmp expect-salutations.c-better actual &&

	show_diff histogram salutations.c >actual &&
	test_cmp expect-salutations.c-better actual
'

test_expect_success 'basic insert a function, tweak a function, remove a function' '
	cat <<-\EOF >expect-tweak.c-lame &&
		diff --git a/tweak.c b/tweak.c
		--- a/tweak.c
		+++ b/tweak.c
		@@ -1,14 +1,20 @@
		-int twiddle(struct point *p1, int factor)
		+int slack(char *msg, int count)
		 {
		-    if (factor > 0)
		+    if (count)
		     {
		-        p1.y *= factor;
		+        puts(msg);
		     }
		 
		     return 0;
		 }
		 
		-void add(int a, int b)
		+int twiddle(struct point *p1, int factor)
		 {
		-    return a + b;
		+    if (factor != 0)
		+    {
		+        p1.x *= factor;
		+        p1.y *= factor;
		+    }
		+
		+    return 0;
		 }
		EOF

	cat <<-\EOF >expect-tweak.c-good &&
		diff --git a/tweak.c b/tweak.c
		--- a/tweak.c
		+++ b/tweak.c
		@@ -1,14 +1,20 @@
		+int slack(char *msg, int count)
		+{
		+    if (count)
		+    {
		+        puts(msg);
		+    }
		+
		+    return 0;
		+}
		+
		 int twiddle(struct point *p1, int factor)
		 {
		-    if (factor > 0)
		+    if (factor != 0)
		     {
		+        p1.x *= factor;
		         p1.y *= factor;
		     }
		 
		     return 0;
		 }
		-
		-void add(int a, int b)
		-{
		-    return a + b;
		-}
		EOF

	show_diff myers tweak.c >actual &&
	test_cmp expect-tweak.c-lame actual &&

	show_diff minimal tweak.c >actual &&
	test_cmp expect-tweak.c-lame actual &&

	show_diff patience tweak.c >actual &&
	test_cmp expect-tweak.c-good actual &&

	show_diff histogram tweak.c >actual &&
	test_cmp expect-tweak.c-lame actual
'

test_expect_success 'remove a function, add a function, tweak a function' '
	cat <<-\EOF >expect-operations.c-lame &&
		diff --git a/operations.c b/operations.c
		--- a/operations.c
		+++ b/operations.c
		@@ -1,18 +1,22 @@
		-int add(int a, int b)
		+int div(int a, int b)
		 {
		+    /* TODO: What were the special cases again? */
		+    if (a == 0)
		+    {
		+        return 0xdeadbeef
		+    }
		     log();
		-    return a + b;
		+    return a / b;
		 }
		 
		-int div(int a, int b)
		+int mul(int a, int b)
		 {
		-    if (a == b)
		+    if (a > 65535 || b > 65535)
		     {
		-        return 1;
		+        return MAX_INT;
		     }
		     log();
		-    return a / b;
		-    /* TODO: What were the special cases again? */
		+    return a * b;
		 }
		 
		 void declaration(void);
		EOF

	cat <<-\EOF >expect-operations.c-good &&
		diff --git a/operations.c b/operations.c
		--- a/operations.c
		+++ b/operations.c
		@@ -1,18 +1,22 @@
		-int add(int a, int b)
		-{
		-    log();
		-    return a + b;
		-}
		-
		 int div(int a, int b)
		 {
		-    if (a == b)
		-    {
		-        return 1;
		-    }
		-    log();
		-    return a / b;
		     /* TODO: What were the special cases again? */
		+    if (a == 0)
		+    {
		+        return 0xdeadbeef
		+    }
		+    log();
		+    return a / b;
		+}
		+
		+int mul(int a, int b)
		+{
		+    if (a > 65535 || b > 65535)
		+    {
		+        return MAX_INT;
		+    }
		+    log();
		+    return a * b;
		 }
		 
		 void declaration(void);
		EOF

	cat <<-\EOF >expect-operations.c-better &&
		diff --git a/operations.c b/operations.c
		--- a/operations.c
		+++ b/operations.c
		@@ -1,18 +1,22 @@
		-int add(int a, int b)
		-{
		-    log();
		-    return a + b;
		-}
		-
		 int div(int a, int b)
		 {
		-    if (a == b)
		+    /* TODO: What were the special cases again? */
		+    if (a == 0)
		     {
		-        return 1;
		+        return 0xdeadbeef
		     }
		     log();
		     return a / b;
		-    /* TODO: What were the special cases again? */
		+}
		+
		+int mul(int a, int b)
		+{
		+    if (a > 65535 || b > 65535)
		+    {
		+        return MAX_INT;
		+    }
		+    log();
		+    return a * b;
		 }
		 
		 void declaration(void);
		EOF

	cat <<-\EOF >expect-operations.c-best &&
		diff --git a/operations.c b/operations.c
		--- a/operations.c
		+++ b/operations.c
		@@ -1,18 +1,22 @@
		-int add(int a, int b)
		-{
		-    log();
		-    return a + b;
		-}
		-
		 int div(int a, int b)
		 {
		-    if (a == b)
		+    /* TODO: What were the special cases again? */
		+    if (a == 0)
		     {
		-        return 1;
		+        return 0xdeadbeef
		     }
		     log();
		     return a / b;
		-    /* TODO: What were the special cases again? */
		 }
		 
		+int mul(int a, int b)
		+{
		+    if (a > 65535 || b > 65535)
		+    {
		+        return MAX_INT;
		+    }
		+    log();
		+    return a * b;
		+}
		+
		 void declaration(void);
		EOF

	show_diff myers operations.c >actual &&
	test_cmp expect-operations.c-lame actual &&

	show_diff minimal operations.c >actual &&
	test_cmp expect-operations.c-lame actual &&

	show_diff patience operations.c >actual &&
	test_cmp expect-operations.c-good actual &&

	show_diff histogram operations.c >actual &&
	test_cmp expect-operations.c-better actual
'

test_done
