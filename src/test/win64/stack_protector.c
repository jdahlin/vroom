/* vcc-flags: -fstack-protector-all */
/* -fstack-protector* on Windows (PLAN-win64.md WIN6), against clang's build: the guard is the
   sysroot's libssp variable __stack_chk_guard, read through .refptr.__stack_chk_guard, and the
   driver links -lssp_nonshared -lssp as clang's MinGW toolchain does; every function here has a
   canary (-all), checked on return through recursion, alloca, a large frame and a callback. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *__stack_chk_guard;

#define NI __attribute__((noinline))

static NI int fill(char *b, int n, int c)
{
	memset(b, c, (size_t)n);
	return b[n - 1];
}

static NI int deep(int n)
{
	char buf[24];

	snprintf(buf, sizeof buf, "%d", n);
	return n ? deep(n - 1) + (int)strlen(buf) : 0;
}

static NI int big(int n)
{
	char page[9000];

	fill(page, (int)sizeof page, n);
	return page[8999] + page[0];
}

static NI int with_alloca(int n)
{
	char *p = __builtin_alloca((size_t)n);

	return fill(p, n, 3);
}

static int cmp(const void *a, const void *b)
{
	char t[8];

	memcpy(t, a, 1);
	return t[0] - *(const char *)b;
}

int main(void)
{
	char s[] = "protector";

	qsort(s, strlen(s), 1, cmp);
	printf("guard set: %d\n", __stack_chk_guard != 0);
	printf("%d %d %d %s\n", deep(12), big(2), with_alloca(100), s);
	return 0;
}
