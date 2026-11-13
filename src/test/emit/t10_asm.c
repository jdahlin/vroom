/* opt.c around inline asm with known registers (outputs, inputs, "+r",
 * clobbers, memory operands, fixed register variables), asm goto, labels
 * nothing jumps to in the middle of a block, and dead results (an
 * overflow check whose sum is unused: only its flags live) */
typedef unsigned long u64;
void out(const char *name, u64 v);

#define noinline __attribute__((noinline))

static noinline long ops(long a, long b)
{
	long r, s = a ^ b, t = a - b;

	asm("add %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
	asm("eor %0, %0, %1" : "+r"(s) : "r"(t));
	asm("lsl %0, %0, #3" : "+r"(t));
	return r * 7 + s * 5 + t;
}

static noinline long clob(long a, long b)
{
	long x = a * 3, y = b + 7, z = a ^ b, w;

	w = x + y;	/* copies of these may sit in x9-x15 */
	asm volatile("mov x9, #1\n\tmov x10, #2\n\tmov x11, #3\n\tmov x12, #4\n\tmov x13, #5\n\tmov x14, #6\n\tmov x15, #7"
		     ::: "x9", "x10", "x11", "x12", "x13", "x14", "x15");
	return x * 31 + y * 17 + z * 13 + w;
}

static noinline long clob_cs(long a, long b)
{
	long x = a * 5, y = b - 3, w = x;

	asm volatile("mov x19, #11\n\tmov x20, #12\n\tmov x21, #13\n\tmov x22, #14\n\tmov x23, #15\n\tmov x24, #16\n\t"
		     "mov x25, #17\n\tmov x26, #18\n\tmov x27, #19\n\tmov x28, #20"
		     ::: "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28");
	return x * 7 + y + w * 3;
}

static noinline long memop(long v)
{
	long loc = 5, r;

	r = loc;
	asm volatile("str %1, %0" : "=Q"(loc) : "r"(v * 3));	/* the asm writes loc */
	r = r * 100 + loc;
	asm volatile("ldr %0, %1" : "=r"(r) : "Q"(loc) : "memory");
	return r + loc;
}

static noinline long memop2(long k)
{
	long loc = k, loc2 = k + 1, *p = &loc2;

	asm volatile("str xzr, %0" : "=Q"(loc));	/* loc held k's register before */
	asm volatile("str xzr, [%0]" :: "r"(p) : "memory");
	return loc * 3 + loc2 + k;
}

static noinline long memop3(long k)
{
	long loc = k;

	asm volatile("str xzr, %0" : "=Q"(loc));	/* the asm, not a store record, writes loc */
	return loc + k;
}

static noinline long outw(long v)
{
	long x = (unsigned char)v, y = (unsigned)v;

	asm("mov %0, #-1" : "+r"(x));	/* was zero-extended from 8 */
	asm("movn %0, #0" : "=r"(y));
	return (long)(unsigned char)x * 3 + x + (long)(unsigned)y;
}

static noinline long x0out(long v)
{
	register long r0 asm("x0");

	asm("mov %0, #-1" : "=r"(r0));	/* writes just x0, 64 bits */
	return (long)(unsigned)r0 + (long)(int)r0 * 3 + v;
}

static noinline int ovf(long a, long b, unsigned long c)
{
	long d;
	unsigned long u;

	return __builtin_add_overflow(a, b, &d) * 4 + __builtin_sub_overflow(c, (unsigned long)b, &u) * 2 +
	       __builtin_mul_overflow(a, b, &d);
}

static noinline long fixed(long v)
{
	register long r0 asm("x0") = v;
	register long r1 asm("x1") = v * 2;
	long keep = v + 9;

	asm("add %0, %0, %1" : "+r"(r0) : "r"(r1));
	return r0 * 3 + keep;
}

static noinline long agoto(long v, int k)
{
	long x = v * 3, y = v + 1;

	asm goto("cbnz %w0, %l[two]" :: "r"(k) :: two);
	return x;
two:
	return y + x;
}

static inline long inl(long v) { long t = v + 1; { long u = t * 2; t = u ^ v; } return t; }

static noinline long labels(long v)
{
	long a = v, b;

	{ long t = a + 1; a = t; }
	b = inl(a) + inl(v);
	{ long t2 = b; b = t2 * 3; }
	return a + b;
}

static noinline long deadv(long v, long w)
{
	long unused = v * w + 3, r = v;

	(void)unused;
	if (w > 2) {
		long u2 = v ^ w;

		(void)u2;
		r = r + w;
	}
	return r;
}

void tests(void)
{
	static const long vals[] = { 0, 1, -1, 3, 100, -100, 0x7fffffff, 0x123456789L, -0x80000000L };
	u64 a = 0, b = 0, c = 0;

	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		long v = vals[i], w = vals[(i * 5 + 2) % (sizeof(vals) / sizeof(vals[0]))];

		a = a * 31 + (u64)ops(v, w) + (u64)clob(v, w) + (u64)clob_cs(v, w);
		b = b * 31 + (u64)memop(v) + (u64)fixed(v) + (u64)agoto(v, (int)(i & 1));
		c = c * 31 + (u64)labels(v) + (u64)deadv(v, w) + (u64)memop2(v) + (u64)memop3(v) + (u64)outw(v) + (u64)x0out(v);
		c = c * 31 + (u64)ovf(v, w, (unsigned long)v) + (u64)ovf(0x7fffffffffffffffL, v, 1);
	}
	out("asm", a);
	out("mem", b);
	out("lbl", c);
}
