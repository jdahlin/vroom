/* Floats kept across calls (VIR at -O2): they live in d8-d15, which a call preserves, and the
   function that uses them saves and restores the caller's.  Each caller below holds more doubles
   across a call than there are such registers, and the callee holds its own across calls too, so
   a register not saved, not restored, or restored on only some of the ways out (a return in the
   middle, a sibling call, a loop's exit) changes a sum.  One function also has a vector loop
   between its calls: the vectors may use v8-v15 inside the loop, where there is no call. */
#include <stdio.h>
#define NI __attribute__((noinline))

float va[64], vb[64];
double sink;

NI double leaf(double x) { sink += x; return x * 0.5 + 1; }
NI float leaff(float x) { sink += x; return x * 0.25f - 2; }

NI double keep(double a, double b, double c, int n)
{
	double s = a, t = b, u = c, v = a * b, w = b * c;

	for (int i = 0; i < n; i++) {
		s += leaf(t);
		t = t * 1.5 - leaf(u);
		u += v;
		if (u > 1e6)
			return s + t + u + v + w;	/* (a way out in the middle) */
		v -= w * 0.125;
	}
	return s + t * 2 + u * 3 + v * 4 + w * 5;
}

NI double tail(double a, int n)
{
	double s = a;

	for (int i = 0; i < n; i++)
		s += leaf(s) * 0.01;
	return leaf(s);	/* a sibling call: the d registers go back first */
}

NI float vecmix(float k, int n)
{
	float s = k, t = leaff(k);

	for (int r = 0; r < 3; r++) {
		for (int i = 0; i < n; i++)
			va[i] = vb[i] * k + s;
		s += leaff(va[n / 2] + t);
		t = t * 0.5f + s;
	}
	return s + t;
}

NI double outer(int n)
{
	double x[12], r = 0;

	for (int i = 0; i < 12; i++)
		x[i] = i * 1.25 + 0.5;
	/* twelve doubles live across each call */
	r += keep(x[0], x[1], x[2], n);
	r += tail(x[3], n);
	r += vecmix((float)x[4], n);
	r += keep(x[5], x[6], x[7], 3 * n);
	for (int i = 0; i < 12; i++)
		r += x[i] * (i + 1);
	return r;
}

int main(void)
{
	for (int i = 0; i < 64; i++)
		va[i] = 0, vb[i] = (float)i * 0.5f;
	for (int n = 0; n < 40; n += 3)
		printf("%d: %.6f %.6f %.6f\n", n, outer(n), keep(1000, 3, 7, n), sink);
	return 0;
}
