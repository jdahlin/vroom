/* What reads only the low half of a register (VIR at -O2, the lowering): a 32-bit
   return, a 4-byte integer argument, through block parameters too, so their zero
   extension (mov wN, wN) goes; and a dense switch indexes its table with the
   selector itself (uxtw for a word) instead of a copy in x17 (its cases call: not a
   table of values).  Edge cases: a long return or argument from an int (the
   extension stays, signed and unsigned), char and short arguments (Apple: extended
   to 32 bits by the caller), variadic ints and longs, int values whose upper half
   is garbage (a truncated long), a switch on a negative word, on a long, from 0 and
   from another low bound, its default. */
#include <stdarg.h>
#include <stdio.h>

#define NI __attribute__((noinline))
typedef long long S;
typedef unsigned long long U;

NI int id(int x) { return x; }
NI S lid(S x) { return x; }
NI U ulid(U x) { return x; }
NI int ch(signed char c, unsigned char u, short s, unsigned short us) { return c + u + s + us; }
NI S sum(int n, ...)
{
	va_list ap;
	S t = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		t += i & 1 ? va_arg(ap, S) : va_arg(ap, int);
	va_end(ap);
	return t;
}

/* returns through block parameters: an int from a call, a load, a difference */
NI int pick(const signed char *p, int a, S big)
{
	int r;

	if (a < 0)
		return 0;
	if (a == 3)
		r = id((int)big);	/* the upper half of big is garbage for id */
	else if (a > 5)
		r = p[0];
	else
		r = p[1] - a;
	return r;
}
NI unsigned upick(const unsigned char *p, unsigned a) { return a > 2 ? p[a] : a * 0x80000001u; }
NI S widen(int a, S big) { return a > 0 ? (int)big : (unsigned)(int)big; }	/* both extensions stay */
NI U uwiden(unsigned a) { return a; }

/* arguments: int from a long (upper half garbage), long from an int */
NI S args(S big, int i, unsigned u)
{
	return id((int)big) + lid(i) + ulid(u) + lid((int)big) + ulid((unsigned)big) +
	       ch((signed char)big, (unsigned char)big, (short)big, (unsigned short)big) +
	       sum(4, (int)big, (S)(int)big, i, (S)u);
}

NI int sw(int x)
{
	switch (x) {
	case 0: return id(10) ^ (int)x;
	case 1: return id(11) ^ (int)x;
	case 2: return id(12) ^ (int)x;
	case 3: return id(13) ^ (int)x;
	case 4: return id(14) ^ (int)x;
	case 5: return id(15) ^ (int)x;
	case 7: return id(17) ^ (int)x;
	default: return -1;
	}
}
NI int swl(S x)
{
	switch (x) {
	case 0: return id(20) ^ (int)x;
	case 1: return id(21) ^ (int)x;
	case 2: return id(22) ^ (int)x;
	case 4: return id(24) ^ (int)x;
	case 5: return id(25) ^ (int)x;
	case 6: return id(26) ^ (int)x;
	case 8: return id(28) ^ (int)x;
	default: return -2;
	}
}
NI int swb(int x)	/* low bound 100: the index in x17 */
{
	switch (x) {
	case 100: return id(1) ^ (int)x;
	case 101: return id(2) ^ (int)x;
	case 102: return id(3) ^ (int)x;
	case 103: return id(5) ^ (int)x;
	case 104: return id(8) ^ (int)x;
	case 105: return id(13) ^ (int)x;
	default: return 0;
	}
}
NI int swt(S big) { return sw((int)big) + sw((int)(big >> 32)); }	/* a word from a long: garbage above */

int main(void)
{
	signed char b[2] = { -7, 9 };
	unsigned char ub[8] = { 1, 2, 3, 4, 5, 6, 7, 200 };
	S big = (S)0x123456789abcdefLL, neg = (S)0xfedcba98fffffff9LL;
	S t = 0;

	for (int a = -2; a < 9; a++)
		t = t * 31 + pick(b, a, a & 1 ? big : neg);
	for (unsigned a = 0; a < 8; a++)
		t = t * 31 + upick(ub, a);
	t = t * 31 + widen(1, neg) + widen(-1, neg) + (S)uwiden(0xfffffff0u);
	t = t * 31 + args(big, -5, 0xfffffffbu) + args(neg, 7, 3);
	for (int x = -3; x < 10; x++)
		t = t * 31 + sw(x) + swl(x) + swl((S)x << 32) + swb(x + 98);
	t = t * 31 + sw(-2147483647 - 1) + swt(big) + swt(neg) + swt((S)5 << 32 | 3);
	printf("%lld\n", t);
	return 0;
}
