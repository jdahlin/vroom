/* vcc-flags: -O2 */
/* static functions called once (VIR inlines them whatever their size): several returns, a
 * loop, a struct through a pointer, a nested once-called function, one that is also
 * address-taken (stays a function), and a recursive one */
struct acc { long sum; int n; };

static int classify(int v)
{
	if (v < 0)
		return -1;
	if (v == 0)
		return 0;
	for (int i = 2; i * i <= v; i++)
		if (v % i == 0)
			return 2;
	return 1;
}

static void tally(struct acc *a, const int *p, int n)
{
	for (int i = 0; i < n; i++) {
		int k = classify(p[i]);

		if (k == 2)
			continue;
		a->sum += p[i] * (k + 1);
		a->n++;
	}
}

static int twice(int x) { return 2 * x; }

static int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

int main(void)
{
	static const int v[] = { -3, 0, 1, 2, 4, 7, 9, 11, 12, 13 };
	struct acc a = { 0, 0 };
	int (*f)(int) = twice;

	tally(&a, v, (int)(sizeof v / sizeof v[0]));
	if (a.sum != (-3 * 0) + 0 + 1 * 2 + 2 * 2 + 7 * 2 + 11 * 2 + 13 * 2 || a.n != 7)
		return 1;
	if (f(21) != 42 || fact(5) != 120)
		return 2;
	return 0;
}
