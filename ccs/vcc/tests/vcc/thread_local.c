/* _Thread_local objects in a program with one thread (on wasm without
 * -pthread they are ordinary globals, as LLVM strips TLS).  The bare-metal
 * arm64 runtime sets up no thread pointer, so there they are plain. */
#include <stdio.h>

#ifdef __wasm__
#define TL _Thread_local
#else
#define TL
#endif

TL int tl_init = 41;
static TL long tl_zero;
TL char tl_buf[8] = "abc";

static int *addr(void) { return &tl_init; }

int main(void)
{
	static TL int calls;

	for (int i = 0; i < 5; i++)
		calls++, tl_zero += i;
	++*addr();
	tl_buf[3] = 'd';
	printf("%d %ld %d %s\n", tl_init, tl_zero, calls, tl_buf);
	return !(tl_init == 42 && tl_zero == 10 && calls == 5);
}
