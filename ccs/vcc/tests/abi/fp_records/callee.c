#include "fp.h"

float rf(float a, double b) { return a * (float)b; }
double rd(int i, double d, float f) { return i + d * f; }
long double rld(long double a, int i, long double b) { return a * i - b; }
struct f2 rf2(float k) { struct f2 s = { k, -k }; return s; }
struct f3 rf3(float k) { struct f3 s = { k, k * 2, k * 3 }; return s; }
struct d2 rd2(double k) { struct d2 s = { k, k / 4 }; return s; }
struct di rdi(double d, long i) { struct di s = { d, i }; return s; }
struct id rid(int k) { struct id s = { k, k + 0.5f, k * 1.25 }; return s; }
struct fi3 rfi3(float k) { struct fi3 s = { k, (int)k * 3, 'x' }; return s; }
struct d3 rd3(double k) { struct d3 s = { k, k + 1, k + 2 }; return s; }

double many(double a, double b, double c, double d, double e, double f, double g, double h, double i, float j, double k,
	    struct d2 l, struct f3 m, int n)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10 + k * 11 + l.a * 12 + l.b * 13 +
	       m.a * 14 + m.b * 15 + m.c * 16 + n * 17;
}

double mixed(struct di a, struct id b, struct fi3 c, struct f2 d, long double e, struct d3 f)
{
	return a.d + a.i * 2 + b.i * 3 + b.f * 4 + b.d * 5 + c.f * 6 + c.i * 7 + c.c + d.a * 8 + d.b * 9 + (double)e * 10 +
	       f.a + f.b * 11 + f.c * 13;
}

double vsum(int n, ...)
{
	va_list ap;
	double s = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		switch (va_arg(ap, int)) {
		case 0: s += va_arg(ap, double); break;
		case 1: s += va_arg(ap, int) * 3; break;
		case 2: { struct d2 x = va_arg(ap, struct d2); s += x.a + x.b * 5; break; }
		case 3: { struct di x = va_arg(ap, struct di); s += x.d * 7 + x.i; break; }
		case 4: s += (double)va_arg(ap, long double) * 11; break;
		case 5: { struct f3 x = va_arg(ap, struct f3); s += x.a + x.b * 2 + x.c * 3; break; }
		}
	}
	va_end(ap);
	return s;
}

double back(double (*f)(struct d2, float, long double), double k)
{
	struct d2 x = { k, k * 2 };

	return f(x, (float)k / 2, (long double)k * 3);
}
