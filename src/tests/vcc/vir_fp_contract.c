/* a * b + c within one expression is one fused operation (clang's default
   -ffp-contract=on; VIR at -O2 builds fma, lowered to fmadd/fmsub/fnmadd/fnmsub).
   With a = 1 + 2^-30 and b = 1 - 2^-30 the product is 1 - 2^-60, which rounds to 1:
   a sum that cancels the 1 is 0 when the product was rounded and +-2^-60 when fused.
   Every contractable form must agree (all fused, or none: the direct emitter does
   not contract); what clang does not contract must stay unfused: a product held in
   a variable, the value of an assignment, a product converted before the sum, one
   chosen by ?:, a constant product. */
#define NI __attribute__((noinline))

NI double a1(double a, double b, double c) { return a * b + c; }
NI double a2(double a, double b, double c) { return c + a * b; }
NI double a3(double a, double b, double c) { return a * b - c; }
NI double a4(double a, double b, double c) { return c - a * b; }
NI double a5(double a, double b, double c) { return -(a * b) + c; }
NI double a6(double a, double b, double c) { return -(a * b) - c; }
NI double a7(double a, double b, double c, double d) { return a * b + c * d; }
NI double b1(double a, double b, double c) { c += a * b; return c; }
NI double b2(double a, double b, double c) { c -= a * b; return c; }
NI double b5(double a, double b, double c) { return ((a * b)) + c; }
NI double b9(double a, double b, double c) { return -a * b + c; }
NI double c2(double a, double b, double c) { return c - (-(a * b)); }
NI double c5(double a, int b, double c) { return a * b + c; }
NI double c6(double a, double b, double c) { return c + -(a * b); }
NI double c7(double a, double b, double c) { return -c - a * b; }
NI double c8(double a, double b, double c) { return a * b + -c; }
NI float f1(float a, float b, float c) { return a * b + c; }
NI float f2(float a, float b, float c) { return c - a * b; }
NI double l1(const double *p, int n)	/* in a loop, through memory */
{
	double s = -(double)n;

	for (int i = 0; i < n; i++)
		s += p[0] * p[1];
	return s;
}

/* not contracted */
NI double n1(double a, double b, double c) { double t = a * b; return t + c; }
NI double n2(double a, double b, double c) { double t; return (t = a * b) + c; }
NI double n3(float a, float b, double c) { return a * b + c; }
NI double n4(double a, double b, double c, int x) { return (x ? a * b : b) + c; }
NI double n5(double c) { return 0x1.00000004p0 * 0x1.fffffff8p-1 + c; }
NI double n6(double a, double b, double c) { volatile double t; return (t = a * b) + c; }
NI double n7(double a, double b, double c) { double t = a * b, u = t; return u + c + (t - t); }

int main(void)
{
	const double a = 0x1.00000004p0, b = 0x1.fffffff8p-1, p = -0x1p-60;	/* a * b = 1 + p */
	const float fa = 0x1.0008p0f, fb = 0x1.fffp-1f, fp = -0x1p-26f;	/* fa * fb = 1 + fp */
	static const double m[2] = { 0x1.00000004p0, 0x1.fffffff8p-1 };
	int nf = 0, n = 0;

	/* r is the fused result or the rounded one, counted */
#define T(r, fused, plain) do { __typeof__(r) r_ = (r); n++; if (r_ == (fused)) nf++; else if (r_ != (plain)) return 100 + n; } while (0)
	T(a1(a, b, -1), p, 0);
	T(a2(a, b, -1), p, 0);
	T(a3(a, b, 1), p, 0);
	T(a4(a, b, 1), -p, 0);
	T(a5(a, b, 1), -p, 0);
	T(a6(a, b, -1), -p, 0);
	T(a7(a, b, -1, 1), p, 0);
	T(b1(a, b, -1), p, 0);
	T(b2(a, b, 1), -p, 0);
	T(b5(a, b, -1), p, 0);
	T(b9(a, b, 1), -p, 0);
	T(c2(a, b, -1), p, 0);
	T(c5(0x1.0000000000001p0, 3, -3), 0x1.8p-51, 0x1p-50);	/* 3 + 3 ulp/2 rounds to 3 + 2^-50 */
	T(c6(a, b, 1), -p, 0);
	T(c7(a, b, -1), -p, 0);
	T(c8(a, b, 1), p, 0);
	T(f1(fa, fb, -1), fp, 0);
	T(f2(fa, fb, 1), -fp, 0);
	T(l1(m, 1), p, 0);
	if (nf && nf != n)
		return 1;	/* some forms fused, some not */
	/* signs of zero: fmsub and fnmadd are not a negated fmadd */
	if (__builtin_copysign(1, a4(1, 0.0, 0.0)) != 1 || __builtin_copysign(1, a6(1, 0.0, 0.0)) != -1 ||
	    __builtin_copysign(1, a3(1, 0.0, 0.0)) != 1 || __builtin_copysign(1, a5(1, 0.0, -0.0)) != -1)
		return 2;
	if (n1(a, b, -1) != 0) return 4;
	if (n2(a, b, -1) != 0) return 5;
	if (n3(fa, fb, -1) != 0) return 6;
	if (n4(a, b, -1, 1) != 0 || n4(a, b, -1, 0) != b - 1) return 7;
	if (n5(-1) != 0) return 8;
	if (n6(a, b, -1) != 0) return 9;
	if (n7(a, b, -1) != 0) return 10;
	return 0;
}
