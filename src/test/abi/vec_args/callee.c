#include "vec.h"

v4si add4(v4si a, v4si b) { return a + b * 10; }

v8qi mix8(int i, v8qi a, double d, v4hi h)
{
	v8qi r = a;

	r[0] += (unsigned char)i;
	r[1] += (unsigned char)d;
	r[2] += (unsigned char)h[3];
	return r;
}

v4sf fma4(v4sf a, v4sf b, v4sf c, struct hfa2 s) { return a * b + c + s.x - s.y; }

v2df many(v2df a, v2df b, v2df c, v2df d, v2df e, v2df f, v2df g, v2df h, v2df i, long tail)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + (double)tail;
}

struct hva2 swap2(struct hva2 h) { struct hva2 r = { h.b, h.a }; return r; }

struct hva3 rot3(struct hva3 h) { struct hva3 r = { h.c, h.a, h.b }; return r; }

struct hva4 sum4(struct hva4 h, struct hva4 k)
{
	struct hva4 r = { { h.a[0] + k.a[0], h.a[1] + k.a[1] }, h.b + k.b, h.c - k.c };

	return r;
}

struct mix mixed(struct mix m, v8si big)
{
	m.a += (v4si){ big[0], big[1], big[2], big[7] };
	m.d *= 2;
	return m;
}

v8si big8(v8si a) { return a * 3; }

int small4(v4qi a, v4qi b) { return a[0] * 1000 + a[3] * 100 + b[1] * 10 + b[2]; }

long vsum(int n, ...)
{
	va_list ap;
	long s = 0;

	va_start(ap, n);
	for (int k = 0; k < n; k++) {
		v4si v = va_arg(ap, v4si);

		s += v[0] + v[1] * 10 + v[2] * 100 + v[3] * 1000;
	}
	va_end(ap);
	return s;
}

v4si call_back(v4si (*f)(v4si, v8qi), v4si a)
{
	v8qi q = { 1, 2, 3, 4, 5, 6, 7, 8 };

	return f(a, q) + 1;
}
