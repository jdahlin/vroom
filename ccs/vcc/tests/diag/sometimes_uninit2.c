/* diag-flags: -Wall */
int g(int);
int t1(int c) { int y; switch (c) { case 1: y = 1; break; case 2: break; } return y; }
int t2(int c) { int y; switch (c) { case 1: y = 1; break; default: break; } return y; }
int t3(int c) { int y; while (c) { y = 1; c--; } return y; }
int t4(int c) { int y; for (; c; c--) y = 1; return y; }
int t5(int c, int d) { int y; if (c && (y = d)) g(0); return y; }
int t6(int c, int d) { int y; if (c || (y = d)) g(0); return y; }
int t7(int c) { int y; c ? (y = 1) : 0; return y; }
int t8(int c) { int y; switch (c) { case 1: case 2: y = 1; break; case 3: case 4: g(0); break; default: y = 3; } return y; }
int t9(int c) { int y; switch (c) { case 1: y = 1; break; case 2: y = 2; break; } return y; }
int u1(int c, int d) { int y; if (c) y = 1; if (d) y = 2; else g(0); return y; }
int u2(int c) { int y; switch (c) { case 1: y = 1; break; case 3: g(0); break; case 4: break; } return y; }
