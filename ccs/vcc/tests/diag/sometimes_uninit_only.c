/* diag-flags: -Wsometimes-uninitialized */
int g(int);
int f(int c) { int x; if (c) x = 1; return x; }
int h(int c) { int x; do { if (c) break; x = c; } while (0); return x; }
