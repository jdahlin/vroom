/* opt.c's known extensions: values extended on one path of a join and not
 * the other, loops that widen a value, asm goto and label-address targets,
 * shifts (lsr, asr, lsl), masks, csetm, 32-bit arithmetic that wraps,
 * narrow call results and arguments, a call that leaves garbage in x0 */
typedef unsigned long u64;
void out(const char *name, u64 v);

#define noinline __attribute__((noinline))

static noinline signed char ret_sc(long v) { return (signed char)v; }
static noinline unsigned short ret_us(long v) { return (unsigned short)v; }
static noinline int ret_i(long v) { return (int)v; }

/* x0 = -1 whatever it held (a caller must not keep what it knew of x0) */
asm(".text\n.globl raw_ret\n.type raw_ret, %function\nraw_ret:\n\tmovn x0, #0\n\tret\n");
long raw_ret(long);

static noinline long join(long v, int k)
{
	long x;

	if (k & 1)
		x = (unsigned char)v;	/* zero-extended from 8 */
	else if (k & 2)
		x = (signed char)v;	/* sign-extended from 8 */
	else
		x = v;			/* anything */
	return (long)(unsigned char)x + (long)(signed char)x * 3 + (long)(unsigned)x + (long)(int)x * 5;
}

static noinline long widen(long v, int n)
{
	unsigned char c = (unsigned char)v;
	long x = c, r = 0;

	for (int k = 0; k < n; k++) {
		r = r * 7 + (unsigned char)x + (unsigned short)x + (signed char)x + (short)x + (int)x;
		x = x * 3 + 1;	/* grows past 8, 16, 32 bits */
	}
	return r;
}

static noinline long shifts(long v)
{
	unsigned char c = (unsigned char)v;
	signed char s = (signed char)v;
	int i = (int)v;
	unsigned u = (unsigned)v;
	long r = 0;

	r += (unsigned char)(c >> 1) + (signed char)(c << 1) + (short)(s >> 2) + (unsigned short)(s << 3);
	r = r * 31 + (int)(u >> 3) + (int)(i >> 5) + (signed char)(i >> 24) + (unsigned char)(u >> 24);
	r = r * 31 + (long)(int)(u << 4) + (long)(unsigned)(i << 4) + (long)(short)((unsigned)i >> 16);
	r = r * 31 + (signed char)((long)s >> 1) + (unsigned char)((unsigned long)v >> 56) + (int)(v >> 32);
	r = r * 31 + (long)((int)u >> 4) + (long)((int)(u & 0xfffffff0u) >> 1);	/* asr of a 32-bit unsigned */
	return r;
}

static noinline long masks(long v)
{
	long r = 0;

	r += (unsigned char)(v & 0x7f) + (signed char)(v & 0xff) + (short)(v & 0xffff) + (int)(v & 0xffffffffL);
	r = r * 31 + (signed char)(v & -16L) + (int)(v | 0x80) + (unsigned char)(v ^ 0x3c) + (short)(v & ~0xffL);
	r = r * 31 + (signed char)((v & 0xf) + (v & 0x70)) + (unsigned char)((v & 0x7f) + (v & 0x7f));
	{
		signed char sc = (signed char)v;

		r = r * 31 + (signed char)((long)sc & ~0xffL) + (short)((long)sc & ~0x7fffL) + (signed char)((long)sc & ~0x7fL);
	}
	return r;
}

static noinline long wrap32(int a, int b, unsigned c)
{
	long r = 0;

	r += (long)(int)(a + b) + (long)(unsigned)(a + b) + (long)(int)(a * b) + (long)(int)(c - 1);
	r = r * 31 + (long)(int)(c + 0x7fffffffU) + (signed char)(a + 1) + (unsigned short)(b - 1);
	r = r * 31 + (long)(int)(a - b) + (long)(int)((unsigned char)a + (unsigned char)b);
	r = r * 31 + (long)(-(a < b)) + (long)(unsigned)(-(a < b)) + (signed char)-(a > b);
	return r;
}

static noinline long carry(long v, long w)
{
	unsigned char c = (unsigned char)v, d = (unsigned char)w;
	unsigned short h = (unsigned short)v, g = (unsigned short)w;

	return (unsigned char)(c + d) + (long)(unsigned short)(h + g) * 3 + (long)(unsigned char)(c + 200) * 5 +
	       (long)(signed char)(c + d) * 7 + (long)(short)((long)h + g) * 11;
}

static noinline long calls(long v)
{
	long r = (unsigned char)raw_ret((unsigned char)v) + (long)(signed char)raw_ret((signed char)v) * 3 +
		 (long)(int)raw_ret((unsigned)v) * 5;

	return r * 31 + (long)ret_sc(v) * 7 + (long)ret_us(v) * 5 + (long)ret_i(v) + (unsigned char)ret_i(v) + (int)ret_us(v);
}

static noinline long asmgoto(long v, int k)
{
	long x = (signed char)v;

	asm goto("cbnz %w0, %l[skip]" :: "r"(k) :: skip);
	x = (unsigned char)v;
skip:
	return (long)(signed char)x + (long)(unsigned char)x * 3;
}

static noinline long addr(long v, int k)
{
	static void *const tg[] = { &&a, &&b };
	long x = (unsigned char)v;

	goto *tg[k & 1];
a:
	x = (unsigned long)v >> 3;
b:
	return (long)(unsigned char)x + (long)(int)x * 3;
}

void tests(void)
{
	static const long vals[] = { 0, 1, -1, 127, 128, 255, 256, -128, -129, 0x7fff, 0x8000, 0xffff, -0x8001,
				     0x7fffffff, 0x80000000L, 0xffffffffL, -0x80000001L, 0x123456789abcdefL, -0x123456789abcdefL };
	u64 a = 0, b = 0, c = 0, d = 0;

	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		long v = vals[i], w = vals[(i * 7 + 3) % (sizeof(vals) / sizeof(vals[0]))];

		for (int k = 0; k < 4; k++)
			a = a * 31 + (u64)join(v, k) + (u64)asmgoto(v, k & 1) + (u64)addr(v, k);
		b = b * 31 + (u64)widen(v, 12) + (u64)shifts(v);
		c = c * 31 + (u64)masks(v) + (u64)wrap32((int)v, (int)w, (unsigned)w) + (u64)carry(v, w);
		d = d * 31 + (u64)calls(v);
	}
	out("join", a);
	out("bits", b);
	out("mask", c);
	out("call", d);
}
