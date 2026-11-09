/*
 * Locals aligned to more than 16 bytes: the prologue realigns sp.  vcc used
 * to cap stack alignment at 16, and PostgreSQL's PGIOAlignedBlock (a union
 * with an aligned(4096) member) failed its own Assert in mdextend.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef union {
	__attribute__((aligned(4096))) char data[8192];
	double d;
	long long i;
} block;

struct line { _Alignas(64) int v[4]; };

static int bad;

static void check(const void *p, uintptr_t a, const char *what)
{
	if ((uintptr_t)p % a) {
		printf("%s not %lu-aligned\n", what, (unsigned long)a);
		bad++;
	}
}

/* stack arguments (the outgoing area sits below the realigned locals) */
__attribute__((noinline)) static long many(long a, long b, long c, long d, long e, long f, long g, long h,
					   long i, long j, const char *p)
{
	return a + b + c + d + e + f + g + h + i + j + p[0];
}

__attribute__((noinline)) static long rec(int depth, char pad)
{
	char odd[3] = { pad, pad, pad };	/* skews the frame */
	block b;
	struct line l;
	_Alignas(128) char c[5];
	long r;

	check(&b, 4096, "block");
	check(&l, 64, "struct line");
	check(c, 128, "_Alignas(128)");
	memset(b.data, depth, sizeof b.data);
	l.v[3] = depth;
	c[4] = odd[2];
	r = depth ? rec(depth - 1, (char)(pad + 1)) : 0;
	/* the recursion must not have clobbered this frame */
	if (b.data[0] != depth || b.data[8191] != depth || l.v[3] != depth || c[4] != pad)
		bad++;
	return r + many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, b.data) + depth;
}

int main(void)
{
	long r = rec(4, 'a');

	printf("%ld %d %zu %zu\n", r, bad, _Alignof(block), _Alignof(struct line));
	if (bad || r != 5 * 55 + 2 * (0 + 1 + 2 + 3 + 4))	/* each level: many() = 55 + depth, plus depth */
		abort();
	return 0;
}
