/* D5.1: -Wunused: block-scope variables never named or only assigned (at the scope's end,
   in declaration order), typedefs never named (at the TU's end), static functions and
   variables nothing uses, or names only from sizeof or their own body (-Wunneeded-internal-
   declaration); attributes unused, used and cleanup silence them */
/* diag-flags: -Wunused */
static int g1;
static const int g2 = 1;
static int g3 __attribute__((unused));
static void f1(void) {}
static inline void f2(void) {}
static void f3(void);
static void f4(void) {}
static int f6(void) { return 0; }
static void rec(void) { if (g1) rec(); }
static int a1(void) { return 1; }
static int a2(void) { return a1(); }
void clf(int *);
int use(void) { f4(); return sizeof(f6()); }
void f(int p) {
	int a;
	int b = 1;
	int c __attribute__((unused));
	int d; d = 1;
	int e; (void)e;
	int arr[4];
	int k = sizeof(k);
	int z1, z2;
	extern int ex;
	int q = 0; q++;
	int h = 0; h |= 1, h += 2;
	volatile int v = 0; v++;
	int t; int *pt = &t;
	int n __attribute__((cleanup(clf)));
	for (int i = 0; i < 2; ) i++;
	{ int inner; }
	typedef int unused_t;
	int w = ({ int r; r = 1; });
	(void)w;
}
