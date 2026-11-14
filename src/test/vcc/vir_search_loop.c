/* vcc-flags: -O3 */
/* VIR (-O3, the loop vectorizer): search loops that update a carried value under a condition (a maximum, a minimum,
 * a maximum with its index, in two dimensions, a last match) stay branches and are not
 * vectorized as an ordered reduction alone; the results with ties, negative zero and NaN. */
#include <stdio.h>
#define NI __attribute__((noinline))
#define N 67

static float a[N], m[8][8];

NI static float vmax(void) { float x = a[0]; for (int i = 0; i < N; i++) if (a[i] > x) x = a[i]; return x; }
NI static float vmin(void) { float x = a[0]; for (int i = 0; i < N; i++) if (a[i] < x) x = a[i]; return x; }
NI static int argmax(float *out)
{
	float x = a[0];
	int idx = 0;

	for (int i = 0; i < N; i++)
		if (a[i] > x) {
			x = a[i];
			idx = i;
		}
	*out = x;
	return idx;
}
NI static int argmax2(int *col)
{
	float x = m[0][0];
	int r = 0, c = 0;

	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j++)
			if (m[i][j] > x) {
				x = m[i][j];
				r = i;
				c = j;
			}
	*col = c;
	return r;
}
NI static int lastneg(void) { int k = -1; for (int i = 0; i < N; i++) if (a[i] < 0) k = i; return k; }
NI static double osum(void) { double s = 0; for (int i = 0; i < N; i++) s += a[i]; return s; }

static void show(const char *what)
{
	float x;
	int c, i = argmax(&x), r = argmax2(&c);

	printf("%s: %g %g %d %g %d %d %d %.9g\n", what, vmax(), vmin(), i, x, r, c, lastneg(), osum());
}

int main(void)
{
	for (int i = 0; i < N; i++)
		a[i] = (float)((i * 37) % 23) - 11.5f + (float)i / 64;
	for (int i = 0; i < 64; i++)
		m[i / 8][i % 8] = (float)((i * 29) % 31) - (float)(i % 5);
	show("mixed");
	a[40] = a[13] = 99;
	m[6][1] = m[2][3] = 77;
	show("ties");
	for (int i = 0; i < N; i++)
		a[i] = i & 1 ? 0.0f : -0.0f;
	a[0] = -0.0f;
	printf("zero %d %d\n", 1 / vmax() < 0, 1 / vmin() < 0);
	a[5] = a[5] - a[5] + (a[5] - a[5]) / (a[5] - a[5]);	/* NaN */
	a[9] = 3;
	show("nan");
	return 0;
}
