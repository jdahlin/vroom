/* A compare repeated with the flags of an identical one still set goes (VIR at -O2);
   not at a label a later branch jumps back to: round the loop the flags are another
   compare's.  Edge cases: the loop's own test before the back edge, the same compare
   before the back edge (then it may go: clang's answer either way), two nested back
   edges, a forward-only join where it does go. */
#define NI __attribute__((noinline))

NI int g(long x, long *p, long n) { if (x == 5) return 0; do { if (x < 5) return 1; n -= *p++; } while (n < 3); return 2; }
NI int g2(long x, long *p, long n)
{
	int r = 0;

	if (x == 5)
		return -1;
	for (long i = 0; i < n; i++) {
		if (x > 5)
			r += 3;
		for (long j = 0; j < 2; j++) {
			if (x >= 5)
				r += (int)p[j];
			if (p[j] > n)
				r++;
		}
	}
	return r;
}
NI int g3(long x, long n) { int r = 0; if (x < 7) r = 1; do { if (x < 7) r += 2; n--; if (x < 7) r += 4; } while (n > 0); return r; }
NI int fwd(long x, int c) { int r = 0; if (x < 9) r = 1; if (c) r += 10; if (x < 9) r += 100; return r; }

int main(void)
{
	long p[4] = { -1, -1, -9, 0 };	/* (g stops within it: n - p[0] - p[1] - p[2] >= 3) */

	if (g(5, p, 0) != 0 || g(7, p, 0) != 2 || g(7, p, -4) != 2 || g(3, p, 0) != 1) return 1;
	if (g2(7, p, 3) != 9 + 3 * (-2) + 0 || g2(5, p, 1) != -1 || g2(4, p, 2) != 0) return 2;
	if (g3(3, 2) != 1 + 12 || g3(8, 3) != 0) return 3;
	if (fwd(1, 1) != 111 || fwd(1, 0) != 101 || fwd(9, 1) != 10) return 4;
	return 0;
}
