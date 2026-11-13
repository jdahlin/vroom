/* diag-flags: -Wall */
int g(int);
int f1(int c) { int x; if (c) { x = 1; } else { return x; } return x; }
int f2(int c) { int x; switch (c) { case 1: x = 1; break; case 2: return x; } return 0; }
int f3(int c) { int x; while (c) { if (c > 3) x = 1; else g(x); c--; } return 0; }
int f4(int c) { int x; for (;;) { if (c) { g(x); break; } x = 2; } return 0; }
int f5(int c) { int x; do { x = c; } while (0); return x; }
int f6(int c) { int x; do { if (c) break; x = c; } while (0); return x; }
int f7(int c) { int x; do { if (c) continue; x = c; } while (0); return x; }
int f8(int c) { int x; do { if (c) { x = 1; break; } g(0); } while (0); return x; }
int f9(int c) { int x; do { if (c) break; else return 0; } while (0); return x; }
int f10(int c) { int x; do { switch (c) { case 1: break; default: x = 1; } } while (0); return x; }
#define M(c) do { if (c) break; } while (0)
int f12(int c) { int x = 0; M(c); return x; }
int k1(int c) { int x; do { if (c) { break; } x = 1; } while (0); return x; }
int k2(int c) { int x; do { if (c) { g(2); break; } x = 1; } while (0); return x; }
int k3(int c) { int x; do { if (c) { c++; break; } x = 1; } while (0); return x; }
int h(int c) { int x; do { if (c) g(1); else { break; } x = 1; } while (0); return x; }
int k(int c) { int x; do { if (c) { g(2); break; } x = 1; } while (0); return x; }
