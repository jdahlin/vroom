/* -Wreturn-stack-address: locals, parameters, arrays, members, ?:, casts */
int *a(void) { int x; return &x; }
int *b(void) { int x[4]; return x; }
int *c(void) { int x[4]; return &x[1]; }
int *d(int p) { return &p; }
int *e(void) { static int x; return &x; }
char *f(void) { char s[] = "hi"; return s; }
const char *g(void) { return "lit"; }
int *h(void) { int x; int *p = &x; return p; }
struct S { int m; } *i(void) { struct S s; return &s; }
int *j(void) { struct S s; return &s.m; }
int *k(int c) { int x, y; return c ? &x : &y; }
void *l(void) { int x; return (void *)&x; }
int *m(void) { register int *r = 0; return r; }
long o(void) { int x; return (long)&x; }
