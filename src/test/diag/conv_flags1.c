/* diag-flags: -Wno-error=incompatible-pointer-types */
/* the function pointer group follows its parent */
void f(int *p);
void g(void) { long l; char *s = 0; int (*fp)(void) = 0; void (*q)(int) = fp; f(&l); s = 1; int x = s; (void)x; (void)q; }
