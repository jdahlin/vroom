/* An add whose second operand is extended in the instruction (add x, x, w, sxtw/uxtw:
   VIR at -O2 folds the extension in) says nothing simple about its result's upper
   bits: the record clean-up took it for a plain 64-bit add of a value known to fit
   32 bits and dropped a later mask or sign extension as redundant.  Edge cases:
   sxtw of a negative word under a 41-bit mask, uxtw of a sign-extended value before
   a sign extension, sub, a shifted extended operand (an index), bytes and halves. */
#define NI __attribute__((noinline))
typedef unsigned long U;

NI U mask_s(unsigned *p, int x, int y) { U a = *p; int b = x + y; U t = a + (long)b; return t & 0x1ffffffffffUL; }
NI long sext_u(short *p, short *q, long *o) { long a = *p; long v = *q; *o = v; long t = a + (U)(unsigned)v; return (int)t; }
NI U mask_sub(unsigned *p, int x, int y) { U a = *p; int b = x ^ y; U t = a - (long)b; return t & 0xffffffffffUL; }
NI U mask_ix(unsigned *p, int x, int y) { U a = *p; int b = x + y; U t = a + ((long)b << 3); return t & 0x3ffffffffffUL; }
NI U mask_b(unsigned *p, signed char *c) { U a = *p; U t = a + (long)*c; return t & 0x1ffffffffUL; }
NI U mask_h(unsigned *p, unsigned short *c) { U a = *p; U t = a - (U)*c; return t & 0x1ffffffffUL; }

int main(void)
{
	unsigned v = 5, big = 0xfffffff0u;
	short s1 = 3, s2 = -4;
	signed char c = -100;
	unsigned short h = 60000;
	long o = 0;

	if (mask_s(&v, -3, -4) != ((5UL + (U)-7L) & 0x1ffffffffffUL)) return 1;
	if (mask_s(&big, 20, 30) != 0x100000022UL) return 2;
	if (sext_u(&s1, &s2, &o) != (int)(3 + 0xfffffffcUL) || o != -4) return 3;
	if (mask_sub(&v, 6, 1) != ((5UL - 7UL) & 0xffffffffffUL)) return 4;
	if (mask_ix(&v, -1, -1) != ((5UL + (U)(-2L * 8)) & 0x3ffffffffffUL)) return 5;
	if (mask_b(&v, &c) != ((5UL + (U)-100L) & 0x1ffffffffUL)) return 6;
	if (mask_h(&v, &h) != ((5UL - 60000UL) & 0x1ffffffffUL)) return 7;
	return 0;
}
