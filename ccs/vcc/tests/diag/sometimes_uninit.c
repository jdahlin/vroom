/* diag-flags: -Wall */
int g(int);
int s1(int c) { int y; if (c) y = 1; return y; }
int s2(int c) { int y; if (c) y = 1; else g(0); return y; }
int s3(int c) { int y; if (c) g(0); else y = 2; return y; }
int s4(int c) { int y; if (c) { y = 1; } return g(y); }
int s5(int c, int d) { int y; if (c) y = 1; if (d) return 0; return y; }
int s6(int c) { int y; if (c) y = 1; return y + y; }
int s3b(int c) { int y; if (c) g(0); else y = 2; return y; }
int s7(int c) { int y; if (c) { y = 1; } else { g(0); } return y; }
int s8(int c) { int y; if (c) { g(0); } else { y = 1; } return y; }
