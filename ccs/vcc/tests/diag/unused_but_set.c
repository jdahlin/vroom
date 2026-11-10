/* diag-flags: -Wall -Wno-unused-value -Wno-self-assign */
int g(int);
void f(int p) {
	int a; a = 1;
	int b = 0; b++;
	int c = 0; c += 2;
	int d = 0; d = d + 1;
	int e; e = 1; g(e);
	int h; (h) = 1;
	int i; for (i = 0; ; ) break;
	int j; j = 1, j = 2;
	volatile int v = 0; v++;
	volatile int w; w = 1;
	int x[2]; x[0] = 1;
	int y = 0; y = 1; (void)sizeof y;
	int z; int *pz = &z; *pz = 1;
	int k = 0; k++; k--;
	int m; m = 3; m = m;
	int n = 0; if (1) n = 2;
	int q; q = g(0);
	struct { int f; } s; s.f = 1;
}
int g2(void) { int x; return 1 ? (x = 1, 2) : (x = 2, 3); }
