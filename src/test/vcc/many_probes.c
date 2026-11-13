/*
 * More distinct include probes than the lexer's file cache has buckets
 * (65536): its open-addressing table filled up and the probe loop never
 * ended.  vpp --batch on PostgreSQL's 1574 TUs (many -I dirs, every missing
 * candidate cached) hung there.  This file includes itself as a binary tree
 * of depth 16; each leaf asks __has_include for a new name.
 */
#define S_(x) #x
#define S(x) S_(x)
#define NAME(n) S(probe_##n.h)
#define NAME_(n) NAME(n)
#if __INCLUDE_LEVEL__ < 16
#include "many_probes.c"
#include "many_probes.c"
#else
#if __has_include(NAME_(__COUNTER__))
#error "probe file exists"
#endif
#endif
#if __INCLUDE_LEVEL__ == 0
#include <stdio.h>

int main(void)
{
	int n = __COUNTER__;

	printf("%d\n", n);
	return n != 65536;
}
#endif
