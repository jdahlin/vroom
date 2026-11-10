/* Constructors run before main, by priority: constructor(N) with a lower N
   first, the ones without a priority (65535) last.  vcc dropped the priority
   (arm64: .init_array.N, wasm: the object's init functions, PLAN-wasm.md W5)
   and wasm dropped constructors altogether. */
#include <stdio.h>

static int order[4], n;

__attribute__((constructor)) static void last(void) { order[n++] = 3; }
__attribute__((constructor(200))) static void second(void) { order[n++] = 2; }
__attribute__((constructor(101))) static void first(void) { order[n++] = 1; }

int main(void)
{
	printf("%d: %d %d %d\n", n, order[0], order[1], order[2]);
	return !(n == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3);
}
