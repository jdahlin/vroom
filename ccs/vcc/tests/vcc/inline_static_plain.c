/* vcc-flags: -O2 */
/* Small static functions not declared inline are inlined at -O2 (as clang);
   recursive, address-taken, noinline and variadic ones stay calls and agree. */
#include <stdarg.h>
#include <stdio.h>

static int get4(const unsigned char *p) { return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int absi(int x) { return x < 0 ? -x : x; }
static int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
static __attribute__((noinline)) int slow(int x) { return x + 1; }
static int addr(int x) { return x * 3; }
static int vsum(int n, ...)
{
	va_list ap;
	int s = 0;

	va_start(ap, n);
	while (n--)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}
static int nest(int a) { return absi(a) + absi(a - 5); }

int (*volatile fp)(int) = addr;

int main(void)
{
	unsigned char b[4] = { 1, 2, 3, 4 };
	int r = get4(b) + absi(-7) + fact(5) + slow(1) + fp(2) + vsum(3, 1, 2, 3) + nest(-2);

	printf("%d %d\n", r, get4(b) == 0x01020304);
	return r != 16909060 + 7 + 120 + 2 + 6 + 6 + 9;
}
