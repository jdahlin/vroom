#include "rec.h"

struct s1 r1(int k) { struct s1 s = { (char)k }; return s; }
struct s3 r3(int k) { struct s3 s = { (char)k, (char)(k + 1), (char)(k + 2) }; return s; }
struct s7 r7(int k) { struct s7 s; for (int i = 0; i < 7; i++) s.a[i] = (char)(k + i); return s; }
struct s12 r12(int k) { struct s12 s = { k, k * 2, k * 3 }; return s; }
struct s16 r16(long k) { struct s16 s = { k, -k }; return s; }
struct s16m r16m(long k) { struct s16m s = { (char)k, k << 20 }; return s; }
struct s24 r24(long k) { struct s24 s = { k, k + 1, k + 2 }; return s; }
struct sbf rbf(int k) { struct sbf s = { k & 7, k * 3, -k }; return s; }

long sum_all(struct s1 a, struct s3 b, struct s4 c, struct s7 d, struct s8 e, struct s12 f, struct s16 g, struct s16m h,
	     struct s24 i, struct sbf j)
{
	long s = a.a + b.a + b.b + b.c + c.a + c.b + e.a + e.b + f.a + f.b + f.c + g.a + g.b + h.a + h.b + i.a + i.b + i.c;

	for (int k = 0; k < 7; k++)
		s += d.a[k] * (k + 1);
	return s + j.a + j.b * 10 + j.c * 100;
}

long spill(long a, long b, long c, long d, long e, struct s16 x, struct s12 y, long f, struct s8 z)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + x.a * 6 + x.b * 7 + y.a * 8 + y.b * 9 + y.c * 10 + f * 11 + z.a * 12 + z.b * 13;
}

long vsum(int n, ...)
{
	va_list ap;
	long s = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		switch (va_arg(ap, int)) {
		case 0: s += va_arg(ap, long); break;
		case 1: { struct s16 x = va_arg(ap, struct s16); s += x.a * 3 + x.b; break; }
		case 2: { struct s24 x = va_arg(ap, struct s24); s += x.a + x.b * 5 + x.c * 7; break; }
		case 3: { struct s3 x = va_arg(ap, struct s3); s += x.a + x.b * 11 + x.c * 13; break; }
		}
	}
	va_end(ap);
	return s;
}

long back(long (*f)(struct s16, struct s3, long), long k)
{
	struct s16 x = { k, k * 2 };
	struct s3 y = { 1, 2, 3 };

	return f(x, y, k + 5);
}
