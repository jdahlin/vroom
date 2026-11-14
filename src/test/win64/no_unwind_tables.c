/* vcc-flags: -fno-asynchronous-unwind-tables */
/* -fno-asynchronous-unwind-tables on Windows (PLAN-win64.md WIN6): as clang, no .seh_* and so no
   .pdata/.xdata for the TU's functions (RtlLookupFunctionEntry finds none), the frames are the
   same and the program runs; a page-sized frame still probes with ___chkstk_ms. */
#include <stdio.h>
#include <string.h>
#include "winapi.h"

#define NI __attribute__((noinline))

static NI int leaf(int x) { return x * 3; }

static NI int paged(int n)
{
	char b[20000];

	memset(b, n, sizeof b);
	return b[19999] + leaf(n);
}

int main(void)
{
	u64 base;

	printf("%d\n", paged(2));
	printf("unwind data for paged: %s\n", RtlLookupFunctionEntry((u64)paged + 4, &base, 0) ? "yes" : "none");
	printf("unwind data for main: %s\n", RtlLookupFunctionEntry((u64)main + 4, &base, 0) ? "yes" : "none");
	return 0;
}
