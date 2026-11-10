/* diag-flags: -Wunused-variable */
void cl(int *p);
#define M int mm
void f(void) {
	int c1 __attribute__((cleanup(cl)));
	volatile int v;
	M;
	int sz; (void)sizeof sz;
	int q; q++;
	int w; w += 1;
	for (int i = 0;;) break;
	int k[2]; k[0] = 1;
	struct S { int x; } s; s.x = 1;
	int *ip; *ip = 1;
	int lab __attribute__((unused)), lab2 = 1; (void)lab2;
}
static int r2;
static int r2;
int use(void) { return r2; }
static const int k1 = 1;
static int z1;
void f3(void) { int q; }
static int z2;
static int arr[] = {1};
static int self = sizeof(self);
