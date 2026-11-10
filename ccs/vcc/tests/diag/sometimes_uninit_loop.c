/* diag-flags: -Wall */
int g(int);
int v1(int c) { int y; while (c) { y = 1; c = g(c); } return y; }
int v2(int c) { int y; for (int i = 0; i < c; i++) y = i; return y; }
int v3(int c) { int y; for (; c; ) { y = 1; break; } return y; }
int v4(int c) { int y; do { if (c) break; y = 1; } while (g(c)); return y; }
int v5(int c) { int y; while (c) { y = 1; if (g(c)) break; } return y; }
int v6(int c) { int y; while (c) { y = 1; break; } return y; }
int v7(int c) { int y; while (c > 2) { if (g(c)) { y = 1; break; } return 0; } return y; }
