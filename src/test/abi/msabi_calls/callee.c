#include "msabi.h"

OTHER int ints(char a, short b, int c, long long d, unsigned char e, long long f, int g)
{
	return a + b * 3 + c * 5 + (int)(d >> 32) + e * 7 + (int)f + g * 11;
}

OTHER double fps(double a, float b, double c, float d, double e, float f) { return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6; }
OTHER double mixed(int a, double b, long long c, float d, int e, double f) { return a + b * 10 + c * 100 + d * 1000 + e * 10000 + f; }
OTHER struct s1 r1(struct s1 a, struct s1 b) { a.c = (char)(a.c + b.c); return a; }
OTHER struct s2 r2(struct s2 a) { a.s = (short)(a.s * 3); return a; }
OTHER struct s3 r3(struct s3 a, int k) { a.c[0] += k; a.c[2] -= k; return a; }
OTHER struct s4 r4(struct s4 a, double k) { a.f = (float)(a.f * k); return a; }
OTHER struct s8 r8(struct s8 a, struct s8 b) { struct s8 r = { a.a + b.b, a.b - b.a }; return r; }
OTHER struct s12 r12(struct s12 a, int k) { a.a += k; a.b *= k; a.c -= k; return a; }
OTHER struct s16 r16(struct s16 a, struct s16 b) { a.d += b.d; a.l -= b.l; return a; }
OTHER struct s24 r24(int k, struct s24 a) { a.a += k; a.b += 2 * k; a.c += 3 * k; return a; }
OTHER struct sd2 rsd2(struct sd2 a) { struct sd2 r = { a.y, a.x }; return r; }
OTHER struct empty_pad rpad(struct empty_pad a, struct empty_pad b, struct empty_pad c, struct empty_pad d, struct empty_pad e)
{
	a.c = (char)(a.c + b.c + c.c + d.c + e.c);
	a.d += b.d + c.d + d.d + e.d;
	return a;
}
OTHER long double ld(long double a, int k, long double b) { return a * k + b; }
OTHER __int128 i128(__int128 a, __int128 b, int k) { return a * k + b; }
OTHER _Complex double cplx(_Complex double a, _Complex float b) { return a * 2 + b; }
OTHER long long many(long long a, long long b, long long c, long long d, long long e, double f, long long g, double h, int i, struct s12 j)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + (long long)(f * 6) + g * 7 + (long long)(h * 8) + i * 9 + j.a + j.b * 10 + j.c * 100;
}
OTHER int callback(OTHER int (*f)(int, double, struct s8), int x) { struct s8 s = { x, x + 1 }; return f(x, x * 0.5, s) + 1; }

/* writes every register its convention lets it (as clang's callee would keep or not) */
OTHER int keep(int x)
{
	volatile double d[12];
	int r = x;

	for (int i = 0; i < 12; i++)
		d[i] = i * 1.5 + x;
	for (int i = 0; i < 12; i++)
		r += (int)d[i];
	return r;
}

#ifndef _WIN32
OTHER double ms_va(int n, ...)
{
	__builtin_ms_va_list ap;
	double s = 0;

	__builtin_ms_va_start(ap, n);
	for (int i = 0; i < n; i++)
		s += i % 2 ? __builtin_va_arg(ap, double) : __builtin_va_arg(ap, int);
	__builtin_ms_va_end(ap);
	return s;
}
#endif
