/* diag-flags: -Wall */
int f(void) { int a = a + 1; void *p = &p; int b = (b = 1); int (*fp)(void); return fp() + a + b; }
int g(void) { unsigned short s; long double ld; float _Complex z; signed char sc; return s + ld + sc + z; }
int h(void) { int x; int y = x; return y; }
