/* Locals with alignment above 16 (TODO.md tier 1, "Over-aligned locals"). */
/* vcc-xfail: Over-aligned locals clamped to 16 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct cl { _Alignas(64) char b[64]; };

static int bad;
#define CHECK(p, a) do { if ((uintptr_t)(p) % (a)) { printf("%s not %d-aligned\n", #p, a); bad = 1; } } while (0)

__attribute__((noinline)) void use(void *p) { memset(p, 0, 1); }

__attribute__((noinline)) void f(int depth, int n)
{
	char pad[3];
	_Alignas(32) int a32;
	_Alignas(64) char a64[10];
	_Alignas(128) long a128;
	_Alignas(4096) char page[16];
	struct cl c;
	struct cl arr[2];
	struct cl vla[n];

	use(pad); use(&a32); use(a64); use(&a128); use(page); use(&c); use(arr); use(vla);
	CHECK(&a32, 32); CHECK(a64, 64); CHECK(&a128, 128); CHECK(page, 4096);
	CHECK(&c, 64); CHECK(&arr[1], 64); CHECK(&vla[0], 64); CHECK(&vla[n - 1], 64);
	if (depth)
		f(depth - 1, n + 1);	/* different sp at every level */
}

int main(void)
{
	char *p = __builtin_alloca(24);

	use(p);
	f(5, 1);
	return bad;
}
