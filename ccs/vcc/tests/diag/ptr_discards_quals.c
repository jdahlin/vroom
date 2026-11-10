const int *gc; int *gp;
void g(const int *cp, int *p) { int *l; l = cp; p = gc; gp = gc; int *m = cp; }
void h(void) { int arr[2]; char *ip = arr; char *jp; jp = arr; }
void take(int *p);
int *ret(const int *q) { take(q); return q; }
const char *s; void use(void) { char *t = s; volatile int *v; int *w = (void)0, *x = v; }
void f2(char **pp, const char **cpp, int *const *ipc, int **ipp, const void *cv, void *v, const struct S *cs) {
 cpp = pp; pp = cpp; ipp = ipc; v = cv; cv = v; char *c = cv; void *w = cs; int *restrict r; int *q = r; const int *const k = 0; int *z = k;
}
void h2(void **vv, int **ii, char **cc2, signed char **sc, unsigned *up, int *ip, const void **cvv) { vv = ii; ii = vv; up = ip; sc = cc2; cvv = vv; void *x = ii; }
void g3(char ***a, const char ***b, char *const **c2, unsigned char **u, char **p) { b = a; a = c2; c2 = a; u = p; }
