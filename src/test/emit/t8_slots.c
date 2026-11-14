/* stack slots through opt.c's forwarding and dead-store removal: narrow
 * types (sign/zero extension), slots reused across scopes, structs and
 * unions with fields of several sizes, address-taken locals written by a
 * callee, one-past-the-end pointers walked backwards, varargs */
typedef unsigned long u64;
void out(const char *name, u64 v);

#define noinline __attribute__((noinline))

struct pt { int x, y; long z; };
union u { u64 q; unsigned int w[2]; unsigned char b[8]; };

static noinline void bump(int *p) { *p += 5; }
static noinline void setpt(struct pt *p, int k) { p->x = k; p->y = k * 2; p->z = (long)k << 33; }
static noinline long opaque(long x) { return x; }

static noinline long narrow(long v)
{
	signed char c = (signed char)v;
	unsigned char uc = (unsigned char)v;
	short s = (short)v;
	unsigned short us = (unsigned short)v;
	int i = (int)v;
	unsigned u = (unsigned)v;
	long r = 0;

	for (int k = 0; k < 3; k++) {	/* (several blocks, loads after stores) */
		r = r * 7 + c + uc + s + us + i + (long)u;
		c++, uc--, s -= 3, us += 3, i ^= 0x55, u = u * 3 + 1;
	}
	return r;
}

static noinline long scopes(int n)
{
	long acc = 0;

	for (int k = 0; k < n; k++) {
		if (k & 1) {
			int a = k * 3;	/* reuses the slot of the struct below */

			bump(&a);
			acc += a;
		} else {
			struct pt p = { k, k + 1, k + 2 };

			acc += p.x + p.y + p.z;
			setpt(&p, k);
			acc += p.x + p.y + (p.z >> 30);
		}
		{
			char tmp = (char)(acc & 0x7f);
			short t2 = (short)(acc * 3);

			acc += tmp + t2;
		}
	}
	return acc;
}

static noinline u64 unions(u64 x)
{
	union u v;
	u64 r;

	v.q = x;
	r = v.w[0] + v.w[1] * 3 + v.b[1] + v.b[7];
	v.w[1] = 0x12345678;
	v.b[0] = 0xaa;
	return r * 31 + v.q;
}

static noinline long backwards(int n)
{
	long arr[6];
	long *p = &arr[6], s = 0;

	for (int k = 0; k < 6; k++)
		arr[k] = k * k + n;
	while (p > arr)
		s = s * 3 + *--p;
	return s;
}

static noinline long vsum(int n, ...)
{
	__builtin_va_list ap;
	long s = 0;

	__builtin_va_start(ap, n);
	for (int k = 0; k < n; k++)
		s = s * 5 + __builtin_va_arg(ap, long);
	__builtin_va_end(ap);
	return s;
}

static noinline long escaped(int k)
{
	int a = k, b = k + 1;
	int *pa = &a;
	long r;

	a = 10;		/* forwarded to the load below only up to the call */
	r = a;
	bump(pa);
	r = r * 100 + a;	/* 15: bump wrote it */
	*pa = 7;
	r = r * 100 + a + b;
	b = (int)opaque(b);
	return r * 100 + b;
}

void tests(void)
{
	u64 h = 0;
	static const long vals[] = { 0, 1, -1, 127, 128, -129, 0x7fff, 0x8000, -0x8001, 0x7fffffff, 0x80000000L, -0x80000001L, 0x123456789abcdefL };

	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
		h = h * 31 + (u64)narrow(vals[i]);
	out("narrow", h);
	out("scopes", (u64)scopes(9));
	out("unions", unions(0x0102030405060708UL) ^ unions(0xfedcba9876543210UL));
	out("back", (u64)backwards(3));
	out("va", (u64)vsum(5, 1L, -2L, 3L, 0x100000000L, -5L));
	out("esc", (u64)escaped(4));
}
