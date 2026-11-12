/* Floating-point loads and stores straight between an FP register and memory
   (VIR at -O2: ldr/str of d and s registers, no general register between).
   Edge cases: offsets the scaled form takes, negative and unaligned ones (ldur),
   offsets past both forms (built in x17), a frame slot written as an integer and
   read as a double and the other way round, volatile locals and volatile through a
   pointer, values spilled across a call, arguments passed and received on the stack. */
#define NI __attribute__((noinline))
typedef unsigned long long U;

static volatile int sink;

struct P { char c; double d; float f; } __attribute__((packed));
struct B { double a[5000]; float f[3]; };

NI double ldd(const double *p) { return p[0] + p[1] + p[4095] + p[-1] + p[-32]; }
NI float ldf(const float *p) { return p[0] + p[1] + p[4095] + p[-1] + p[-64]; }
NI void std(double *p, double x) { p[0] = x, p[1] = x + 1, p[4095] = x + 2, p[-1] = x + 3, p[-32] = x + 4; }
NI void stf(float *p, float x) { p[0] = x, p[1] = x + 1, p[4095] = x + 2, p[-1] = x + 3, p[-64] = x + 4; }
NI double unal(struct P *p, double x, float y)	/* offsets 1 and 9: unscaled */
{
	double o = p->d + p->f;

	p->d = x, p->f = y;
	return o;
}
NI double far(struct B *b, double x)	/* offset 40000 and up: past ldr's and ldur's reach */
{
	double o = b->a[4999] + b->f[2];

	b->a[4999] = x, b->f[2] = (float)x;
	return o + b->a[4096];
}
NI double bigframe(int i, double x)	/* a slot far from sp */
{
	volatile double a[6000];

	a[0] = x, a[5999] = x * 2, a[i] = x * 3;
	return a[0] + a[5999] + a[i];
}

/* the Dekker product of sqlite: every intermediate is a volatile double */
NI void dekker(volatile double *x, double y, double yy)
{
	volatile double tx, ty, p, q, c, cc;
	double hx, hy;
	U m;

	__builtin_memcpy(&m, (void *)&x[0], 8);
	m &= 0xfffffffffc000000ull;
	__builtin_memcpy(&hx, &m, 8);
	tx = x[0] - hx;
	__builtin_memcpy(&m, &y, 8);
	m &= 0xfffffffffc000000ull;
	__builtin_memcpy(&hy, &m, 8);
	ty = y - hy;
	p = hx * hy;
	q = hx * ty + tx * hy;
	c = p + q;
	cc = p - c + q + tx * ty;
	cc = x[0] * yy + x[1] * y + cc;
	x[0] = c + cc;
	x[1] = c - x[0];
	x[1] += cc;
}

/* one slot, integer and FP accesses: neither side may be forwarded past the other */
NI U bits_of(double x, int n)
{
	union { double d; U u; float f; unsigned w; } v;
	U r = 0;

	v.u = 0x1122334455667788ull;
	for (int i = 0; i < n; i++) {
		v.d = x + i;	/* FP store, integer load */
		r ^= v.u;
		v.u = r;	/* integer store, FP load */
		x = v.d * 0.5 + 1;
		v.w = (unsigned)i;	/* the low half only */
		r += (U)(v.f * 0) + v.u;
	}
	return r;
}

NI double spill_mid(double x) { sink++; return x * 0.5; }
NI double spill(const double *p, const float *q)	/* many live across calls */
{
	double a = p[0], b = p[1], c = p[2], d = p[3], e = p[4], f = p[5], g = p[6], h = p[7], i = p[8], j = p[9];
	float s = q[0], t = q[1], u = q[2];
	double r = spill_mid(a + j);

	r += spill_mid(b + s) + c + d + e;
	r += spill_mid(f + t) + g + h + i;
	return r + a * b - c * d + e * f - g * h + i * j + s * t * u;
}

NI double many(double a, double b, double c, double d, double e, double f, double g, double h,
	       double i, float j, double k, float l, int m)
{
	return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j + 11 * k + 12 * l + m;
}
NI double pass(const double *p, const float *q) { return many(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], q[0], p[9], q[1], 7); }

NI float vol(volatile float *p, volatile double *q)
{
	volatile float a = *p;
	volatile double b = *q;

	*p;	/* a read nobody uses */
	*q;
	a = a + (float)b;
	*p = a, *q = b + a;
	return a;
}

static double dv[4200];
static float fv[4200];

int main(void)
{
	double *dp = dv + 64;
	float *fp = fv + 64;

	for (int i = 0; i < 4200; i++)
		dv[i] = i * 0.25 - 100, fv[i] = (float)(i * 0.5 - 50);
	if (ldd(dp) != dp[0] + dp[1] + dp[4095] + dp[-1] + dp[-32]) return 1;
	if (ldf(fp) != fp[0] + fp[1] + fp[4095] + fp[-1] + fp[-64]) return 2;
	std(dp, 1000);
	if (dp[0] != 1000 || dp[1] != 1001 || dp[4095] != 1002 || dp[-1] != 1003 || dp[-32] != 1004 || dp[2] != 66 * 0.25 - 100) return 3;
	stf(fp, 2000);
	if (fp[0] != 2000 || fp[1] != 2001 || fp[4095] != 2002 || fp[-1] != 2003 || fp[-64] != 2004 || fp[2] != 66 * 0.5f - 50) return 4;
	{
		struct P p = { 1, 2.5, 4.25f };

		if (unal(&p, -8.5, 16.5f) != 6.75 || p.c != 1 || p.d != -8.5 || p.f != 16.5f) return 5;
	}
	{
		static struct B b;

		b.a[4999] = 3, b.f[2] = 0.5f, b.a[4096] = 100, b.a[4998] = 9, b.f[1] = 8;
		if (far(&b, 7.75) != 103.5 || b.a[4999] != 7.75 || b.f[2] != 7.75f || b.a[4998] != 9 || b.f[1] != 8) return 6;
	}
	if (bigframe(3000, 1.5) != 1.5 + 3 + 4.5) return 7;
	if (bigframe(0, 1.5) != 4.5 + 3 + 4.5) return 8;
	{
		volatile double x[2] = { 1.0e+10, 1.0e-7 };

		dekker(x, 1.0e-10, -3.6432197315497741579e-27);
		/* (the last bits depend on contraction, which clang does and vcc does not) */
		if (x[0] < 0.9999999999999 || x[0] > 1.0000000000001 || x[1] < -1e-15 || x[1] > 1e-15) return 9;
	}
	{
		U a = bits_of(3.25, 9), b = bits_of(3.25, 9);

		if (a != b || !a) return 10;
		{
			union { double d; U u; float f; unsigned w; } v;
			U r = 0;
			double x = 3.25;

			v.u = 0x1122334455667788ull;
			for (volatile int i = 0; i < 9; i++) {
				v.d = x + i;
				r ^= v.u;
				v.u = r;
				x = v.d * 0.5 + 1;
				v.w = (unsigned)i;
				r += (U)(v.f * 0) + v.u;
			}
			if (a != r) return 11;
		}
	}
	{
		double want, r0, r1, r2;

		sink = 0;
		r0 = dv[0] + dv[9], r1 = dv[1] + fv[0], r2 = dv[5] + fv[1];
		want = r0 * 0.5;
		want += r1 * 0.5 + dv[2] + dv[3] + dv[4];
		want += r2 * 0.5 + dv[6] + dv[7] + dv[8];
		want = want + dv[0] * dv[1] - dv[2] * dv[3] + dv[4] * dv[5] - dv[6] * dv[7] + dv[8] * dv[9] + fv[0] * fv[1] * fv[2];
		if (spill(dv, fv) != want || sink != 3) return 12;
	}
	{
		double want = dv[0] + 2 * dv[1] + 3 * dv[2] + 4 * dv[3] + 5 * dv[4] + 6 * dv[5] + 7 * dv[6] + 8 * dv[7] + 9 * dv[8] +
			      10 * fv[0] + 11 * dv[9] + 12 * fv[1] + 7;

		if (pass(dv, fv) != want) return 13;
	}
	{
		volatile float a = 1.5f;
		volatile double b = 2.25;

		if (vol(&a, &b) != 3.75f || a != 3.75f || b != 6.0) return 14;
	}
	return 0;
}
