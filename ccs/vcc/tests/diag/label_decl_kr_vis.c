/* diag-flags: -std=gnu17 */
int f(int c) {
 l: int x = c;
 if (x) goto l;
 switch (c) { case 1: int y = 2; return y; default: ; }
 return 0;
}
int f2(int c){ switch(c){ case 1: __attribute__((fallthrough)); case 2: __attribute__((unused)) int q = 1; return q; } return 0; }
int k(a) int a; { return a; }
int m(a) struct s { int q; } *a; { return a->q; }
int n(a) struct t *a; { return 0; }
int f3(struct u *p);
int g3(struct v { int a; } *p) { return p->a; }
struct w;
int h3(struct w *p);
void a0(void) { int f9(struct z1 *); }
struct S { void (*fp)(struct z2 *); };
typedef int (*T)(struct z3 *);
void b0(int (*g)(struct z4 *));
void c0(enum e1 x);
void d0(int n, int a[sizeof(struct z5 *)]);
void e0(union u1 *p);
