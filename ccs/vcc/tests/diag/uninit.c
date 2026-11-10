/* D8.5: -Wuninitialized and -Wsometimes-uninitialized: the definite use, the edge that
   leaves the variable uninitialized ('if', '&&', '?:', a loop's exit, a case), the fix-it
   by type (0, 0.0, '\0', NULL), its own initialization; what is not tracked */
/* diag-flags: -Wall -Wno-unused */
#define NULL ((void *)0)
int gg(int);
void h(int *);
int u1(void) { int x; return x; }
int u2(int c) { int x; if (c) x = 1; return x; }
int u3(int c) { int x; if (c) return 0; else x = 2; return x; }
int u4(int c) { int x; switch (c) { case 1: x = 1; break; case 2: break; default: x = 3; } return x; }
int u5(int n) { int x; for (int i = 0; i < n; i++) x = i; return x; }
int u6(int n) { int x; while (n--) x = n; return x; }
int u7(int c) { int x; if (c && (x = gg(1))) return x; return x; }
int u8(void) { int x; h(&x); return x; }
int u9(int c) { int x = x; return x + c; }
int u10(int c) { char *p; double d; if (c) p = 0, d = 1; return *p + (int)d; }
int u11(int n) { int x, s = 0; for (int i = 0; i < n; i++) { s += x; x = i; } return s; }
int u12(int c) { int x; x++; return x; }
int u13(int c) { int x; x += 1; return x; }
int u14(int c) { int x; (void)sizeof(x); return 0; }
int u15(int c) { int x; if (c) goto out; x = 1; out: return x; }
int u16(int c) { int x; do { x = gg(c); } while (c--); return x; }
int u17(int c) { _Bool b; float f; long *q; return b + f + (q != 0); }
int u18(int c) { int x; switch (c) { case 1: x = 1; break; } return x; }
int u19(int c) { int x; return c ? x : 0; }
int u20(int c) { struct { int a; } s; return s.a; }
int u21(int c) { int a[2]; return a[0]; }
int f1(int c) { int *p; return *p; }
unsigned g2(int c) { unsigned u; unsigned char ch; enum E { A } e; return u + ch + e; }
int g3(int c){int x; if (c) { if (c > 1) x = 1; } else x = 2; return x;}
int g4(int c){int x; c ? (x = 1) : 0; return x;}
int g5(int c){int x; if (c) x = 1; if (c) return x; return 0;}
int g6(int c){int x; while (c) { if (g6(c)) { x = 1; break; } } return x;}
int g7(int c){int x; for (;;) { if (c) break; x = 1; } return x;}
int f2(int c) { int x, y; if (c) return y; y = x; }
int g9(int c) { int x; if (c) x = 1; if (c > 2) return 0; return x; }
int h2(int c) { int x; if (c) x = 1; while (c--) g9(c); return x; }
int k2(int c) { int x; if (c) { if (c > 1) x = 1; } else x = 2; if (c) x = 3; return x; }
int m2(int c) { int x; if (c) x = 1; else if (c > 3) x = 2; return x; }
int n2(int c) { int x; switch (c) { case 1: x = 1; break; default: break; } return x; }
int q2(int c) { int x; for (int i = 0; i < c; i++) { x = i; } return x; }
int r2(int c) { int x; if (c || (x = 1)) return 0; return x; }
int s2(int c) { int x; do { if (c) break; x = 1; } while (0); return x; }
