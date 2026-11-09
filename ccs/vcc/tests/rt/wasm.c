/* What the test runtime headers (rt/include) need beyond emscripten's libc,
   for bench/vcc-wasm.py: linked into both clang's and vcc's builds. */
#include <stdio.h>
#include <stdlib.h>

void __rt_assert(const char *e, const char *f, int l, const char *fn)
{
	fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", f, l, fn, e);
	abort();
}
