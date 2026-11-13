/* diag-flags: -Wall */
#define bool _Bool
#define false 0
void g(int *);
void h(const int *);
int a1(void) { int x; g(&x); return x; }
int a2(void) { int x; int *p = &x; return x; }
int a3(void) { int x; h(&x); return x; }
int a4(void) { int x; (void)x; x; return 0; }
int a5(void) { int x; x++; return 0; }
int a6(void) { int x; x += 1; return 0; }
int a7(int c) { int x; if (c) return 0; return x; }
int a8(void) { int x; return 0; return x; }
int a9(void) { int x; L: if (x) goto L; return x; }
char *b1(void) { char *p; return p; }
double b2(void) { double d; return d; }
bool b3(void) { bool d; return d; }
char b4(void) { char d; return d; }
enum E { A }; enum E b5(void) { enum E d; return d; }
int b6(void) { int x; return sizeof x + __builtin_constant_p(x); }
int b7(void) { int x, y; return x + y; }
int b8(void) { int x; return x + x; }
int b9(int c) { int x; for (;;) { if (c) return x; x = 1; } }
int c2(void) { int x; asm("" : "=r"(x)); return x; }
int c3(void) { int x; asm("" :: "r"(x)); return 0; }
int c4(void) { int x; return (x = 1, x); }
int c5(void) { int x; return 0 && x; }
int c6(void) { int x; x, 1; return 0; }
long c7(void) { register long x asm("x1"); return x; }
int c8(void) { volatile int x; return x; }
int c9(void) { int x; struct { int y; } s; s.y = x; return s.y; }
