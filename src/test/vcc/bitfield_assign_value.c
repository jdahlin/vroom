/* The value of a compound assignment or prefix ++/-- on a bit-field is the
   value stored, truncated to the field's width (C11 6.5.16p3).  vcc's arm64
   emitter truncated it for plain `=` only, so `(s.f ^= 40)` on a signed
   4-bit field gave 40 instead of -8 and `++s.u` on 15 gave 16 instead of 0.
   Found by Csmith (seed 3000573), reduced with cvise. */
#include <stdlib.h>

struct S {
	int a;
	signed f : 4;
	unsigned u : 4;
	long long w : 40;
	unsigned long long uw : 63;
};

static struct S g;

__attribute__((noinline)) static int id(int x)
{
	return x;
}

int main(void)
{
	struct S s = { 0 };
	int v = id(40);

	if ((s.f ^= v) != -8)
		abort();
	if ((unsigned char)(s.f ^= v) != 0)
		abort();
	if ((s.f = 40) != -8)
		abort();
	if ((s.f += 9) != 1)
		abort();
	if ((s.u ^= v) != 8)
		abort();
	if ((s.f |= 12) != -3 || s.f != -3)
		abort();
	s.f = 7;
	if (++s.f != -8)
		abort();
	if (s.f-- != -8 || s.f != 7)
		abort();
	s.u = 15;
	if (++s.u != 0)
		abort();
	if (--s.u != 15)
		abort();
	if ((s.w += 0x8000000000LL - 1) != 549755813887LL)
		abort();
	if ((s.w <<= 4) != -16)
		abort();
	if ((s.uw -= 1) != 0x7fffffffffffffffULL)
		abort();

	/* the reduced Csmith case: the result goes on through a conversion */
	g.f = 0;
	g.a = (unsigned char)(g.f ^= id(40));
	if (g.a != 248)
		abort();
	return 0;
}
