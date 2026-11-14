/* constructors and destructors (DT_INIT_ARRAY, DT_FINI_ARRAY), atexit, TLS
   (initial-exec relaxed to local-exec in the executable) */
#include <stdio.h>
#include <stdlib.h>

static int order;
static __thread int tl = 3;
__thread long tg[4] = { 1, 2, 3, 4 };

__attribute__((constructor(101))) static void c1(void) { order = order * 10 + 1; }
__attribute__((constructor)) static void c2(void) { order = order * 10 + 2; }
__attribute__((destructor)) static void d1(void) { printf("destructor %d\n", order); }
static void bye(void) { puts("atexit"); }

int main(void)
{
	atexit(bye);
	tl += 4;
	tg[3] += tl;
	printf("ctors %d tls %d %ld\n", order, tl, tg[3]);
	return 0;
}
