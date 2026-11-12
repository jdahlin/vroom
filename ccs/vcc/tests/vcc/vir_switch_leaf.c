/* A function whose switch is a jump table and that calls nothing, or only makes sibling
 * calls, needs no frame record: the table's dispatch writes x16 and x17 only.  With
 * callee-saved registers or spill slots in use it keeps what it needs of a frame. */
#include <stdio.h>
#define NI __attribute__((noinline))

struct U { long a; long v; short n; signed char d, r1, r2; char eq; };

NI static int skip(int n, const unsigned char *k, struct U *p, int f) { return n + k[0] + p->n + f; }

NI static int cmpi(int n, const unsigned char *k, struct U *p)
{
	const unsigned char *a = &k[k[0] & 0x3f];
	long v;

	switch (k[1]) {
	case 1: v = (signed char)a[0]; break;
	case 2: v = (short)(a[0] << 8 | a[1]); break;
	case 3: v = (a[0] << 16 | a[1] << 8 | a[2]); break;
	case 4: v = (int)((unsigned)a[0] << 24 | a[1] << 16 | a[2] << 8 | a[3]); break;
	case 8: v = 0; break;
	case 9: v = 1; break;
	case 0: case 7: return skip(n, k, p, 0);
	default: return skip(n, k, p, 2);
	}
	if (p->v > v) return p->r1;
	if (p->v < v) return p->r2;
	if (p->n > 1) return skip(n, k, p, 1);
	p->eq = 1;
	return p->d;
}

/* a leaf with a table and more live values than caller-saved registers hold easily */
NI static long mix(int op, long a, long b, long c, long d, long e, long f, long g, long h)
{
	long x = a * 3 + b, y = c * 5 + d, z = e * 7 + f, w = g * 11 + h;
	long p = a ^ c, q = b ^ d, r = e ^ g, s = f ^ h, t = a + h, u = b + g, v = c + f, m = d + e;

	switch (op) {
	case 0: x += y; break;
	case 1: x -= z; break;
	case 2: x ^= w; break;
	case 3: x += p * q; break;
	case 4: x -= r * s; break;
	case 5: x ^= t * u; break;
	case 6: x += v * m; break;
	case 7: x = -x; break;
	default: x = 0; break;
	}
	return x + y + z + w + p + q + r + s + t + u + v + m;
}

/* the table and a real call: the frame stays */
NI static long both(int op, long a, long b)
{
	long r;

	switch (op) {
	case 0: r = a + b; break;
	case 1: r = a - b; break;
	case 2: r = a * b; break;
	case 3: r = a ^ b; break;
	case 4: r = a | b; break;
	case 5: r = a & b; break;
	default: r = 0; break;
	}
	return r + mix(op, a, b, r, a, b, r, a, b) + a;
}

int main(void)
{
	static const unsigned char keys[][8] = {
		{ 2, 1, 0xfe, 0, 0, 0, 0, 0 }, { 2, 2, 0x12, 0x34, 0, 0, 0, 0 }, { 2, 3, 1, 2, 3, 0, 0, 0 },
		{ 2, 4, 0x80, 0, 0, 1, 0, 0 }, { 2, 8, 0, 0, 0, 0, 0, 0 }, { 2, 9, 0, 0, 0, 0, 0, 0 },
		{ 2, 0, 0, 0, 0, 0, 0, 0 }, { 2, 7, 0, 0, 0, 0, 0, 0 }, { 2, 5, 0, 0, 0, 0, 0, 0 }, { 2, 200, 0, 0, 0, 0, 0, 0 },
	};

	for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++)
		for (long v = -2; v <= 2; v++)
			for (int n = 1; n <= 2; n++) {
				struct U u = { 0, v, (short)n, 7, 1, -1, 0 };

				printf("%d/%d ", cmpi(n, keys[i], &u), u.eq);
			}
	printf("\n");
	for (int op = -1; op < 10; op++)
		printf("%ld %ld\n", mix(op, 1, 2, 3, 4, 5, 6, 7, 8), both(op, 100 + op, 7));
	return 0;
}
