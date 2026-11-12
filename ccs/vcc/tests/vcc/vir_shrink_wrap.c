/* VIR: shrink-wrapping.  The frame is set up at the block that dominates what needs it;
 * returns and sibling calls in front of it use none.  The callers keep values in
 * callee-saved registers across calls that take the early and the framed way, so a
 * register written in front of the prologue, or a return behind it without the epilogue,
 * changes the sums. */
#include <stdio.h>
#define NI __attribute__((noinline))

static long calls;
NI static long work(long *p, long n) { calls++; return p[0] * 7 + n; }
NI static void note(void *p) { calls += p != 0; }
static void (*hook)(void *) = note;

/* early returns, then calls */
NI static long early(long *p, long n)
{
	if (!p)
		return -1;
	if (n < 4)
		return p[0];
	long a = work(p, n);

	return a + work(p + 1, n - 1) + n;
}

/* a return in front, a sibling call in front, the frame on one way only */
NI static void rel(void *p, int stat)
{
	if (p == 0)
		return;
	if (stat) {
		note(p);
		calls -= 1;
		note(p);
	}
	hook(p);
}

/* the save point is a block that only branches into a loop with calls */
struct W { long (*cb)(struct W *, long *); long (*sel)(struct W *, long *); long depth; };
NI static long cb1(struct W *w, long *p) { calls++; return p[0] == 99 ? 2 : p[0] == 98; }
NI static long walk(struct W *w, long *p)
{
	if (p == 0)
		return 0;
	if (w->cb == 0)
		return 0;
	do {
		long rc = w->cb(w, p);

		if (rc)
			return rc & 2;
		if (work(p, w->depth) < 0 || work(p + 1, 1) == -5)
			return 2;
		if (w->sel)
			w->sel(w, p);
		p = (long *)p[3];
	} while (p);
	return 0;
}

/* the returned value joins from both sides of the save point */
NI static long joined(long *p, long n)
{
	long r;

	if (n == 0)
		r = 11;
	else if (n == 1)
		r = p[0] + 1;
	else
		r = work(p, n) + work(p, -n) + p[1];
	return r;
}

/* more values live across the framed part than caller-saved registers on the way in */
NI static long wide(long a, long b, long c, long d, long e, long f, long g, long h)
{
	if (a == b)
		return c + d;
	long x = work(&a, b), y = work(&c, d), z = work(&e, f);

	return x + y + z + a + b + c + d + e + f + g + h;
}

/* a loop in front of the calls: the frame cannot be inside it */
NI static long scan(long *p, long n)
{
	long i, s = 0;

	for (i = 0; i < n; i++) {
		if (p[i] < 0)
			return -i;
		s += p[i];
	}
	if (s > 100)
		s = work(p, s) + work(p + 1, i);
	return s;
}

NI static long drive(long *p, const long *q0, long n, long k)
{
	long q[8];

	for (int i = 0; i < 8; i++)
		q[i] = q0[i];
	long a = p[0] + k, b = p[1] * 3, c = p[2] ^ k, d = n * 5, e = k - 9, f = a + b, g = c - d, h = e * f;
	struct W w = { cb1, 0, k };
	long s = 0;

	for (long i = 0; i < 6; i++) {
		s += early(i & 1 ? q : 0, n + i) + early(q, i) + joined(q, i % 4) + wide(a, b + i, c, d, e, f, g, i);
		rel(i & 2 ? p : 0, (int)(i & 1));
		s += walk(&w, i & 1 ? p : 0) + walk(&w, p + 4) + scan(q, i + 2) + scan(q + 4, 4);
		s += a + b * 2 + c * 3 + d * 5 + e * 7 + f * 11 + g * 13 + h * 17;
	}
	return s;
}

int main(void)
{
	long p[12] = { 5, 12, 40, 0, 7, 99, 60, 0, 98, 1, -3, 0 };

	static const long q[8] = { 5, 12, 40, 3, 70, 99, -60, 2 };

	p[3] = (long)&p[4];
	p[7] = (long)&p[8];
	printf("%ld %ld\n", drive(p, q, 3, 4), calls);
	printf("%ld %ld\n", drive(p, q, 9, -2), calls);
	return 0;
}
