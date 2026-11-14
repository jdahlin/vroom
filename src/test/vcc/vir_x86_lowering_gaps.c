/* What x86-64's VIR lowering refused after a whole build, and the direct
   emitter then compiled again: f64 of an unsigned 64-bit value (hi * 2^32 +
   lo, one rounding), popcnt (with POPCNT), atomics narrower than their
   result (xchg/cmpxchg at the access width, the old value zero-extended),
   and a function calling setjmp (refused when the builder sees the call).
   Each is checked against values computed another way. */
#include <setjmp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NI __attribute__((noinline))
static NI double u2d(uint64_t x) { return (double)x; }
static NI int pc32(unsigned x) { return __builtin_popcount(x); }
static NI int pc64(unsigned long long x) { return __builtin_popcountll(x); }
static NI int par64(unsigned long long x) { return __builtin_parityll(x); }
static NI unsigned char xchg8(_Atomic unsigned char *p, unsigned char v) { return atomic_exchange(p, v); }
static NI unsigned short xchg16(_Atomic unsigned short *p, unsigned short v) { return atomic_exchange(p, v); }
static NI unsigned char add8(_Atomic unsigned char *p, unsigned char v) { return atomic_fetch_add(p, v); }
static NI bool cas8(_Atomic unsigned char *p, unsigned char *exp, unsigned char v) { return atomic_compare_exchange_strong(p, exp, v); }
static NI bool casb(_Atomic bool *p, bool exp, bool v) { return atomic_compare_exchange_strong(p, &exp, v); }
static jmp_buf jb;
static NI void jump(int v) { longjmp(jb, v); }
static NI int with_setjmp(volatile int n) { int r = setjmp(jb); if (!r) jump(n + 1); return r; }

static int slowpop(unsigned long long x) { int n = 0; for (; x; x >>= 1) n += (int)(x & 1); return n; }

int main(void)
{
	static const uint64_t u[] = { 0, 1, 0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull,
		0xfffffffffffffbffull, 0xfffffffffffffc00ull, 0xffffffffffffffffull, 0x123456789abcdef1ull, 1ull << 53 | 1, 0x20000000000003ull };
	int bad = 0;

	for (unsigned k = 0; k < sizeof u / sizeof *u; k++) {
		/* reference: halves in long double precision is not portable; use the sign trick clang uses */
		uint64_t x = u[k];
		double ref = (int64_t)x >= 0 ? (double)(int64_t)x : 2.0 * (double)(int64_t)((x >> 1) | (x & 1));

		bad |= u2d(x) != ref;
		bad |= pc64(x) != slowpop(x) || pc32((unsigned)x) != slowpop((unsigned)x) || par64(x) != (slowpop(x) & 1);
	}
	_Atomic unsigned char c = 0xfe;
	_Atomic unsigned short h = 0xfedc;
	_Atomic bool b = true;
	unsigned char e = 0xfe;

	bad |= xchg8(&c, 0x80) != 0xfe || c != 0x80;
	bad |= xchg16(&h, 0x8001) != 0xfedc || h != 0x8001;
	bad |= add8(&c, 0x90) != 0x80 || c != 0x10;
	bad |= cas8(&c, &e, 1) || e != 0x10 || c != 0x10;		/* fails: e gets the old value */
	bad |= !cas8(&c, &e, 0xff) || c != 0xff;
	bad |= !casb(&b, true, false) || b || casb(&b, true, true) || b;
	bad |= with_setjmp(41) != 42;
	printf("%s\n", bad ? "FAIL" : "ok");
	return bad;
}
