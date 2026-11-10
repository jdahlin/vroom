/* diag-flags: -Wno-error=int-conversion -Werror */
/* -Werror after -Wno-error=: still a warning */
void f(int *p);
void g(void) { long l; char *s = 0; int (*fp)(void) = 0; void (*q)(int) = fp; f(&l); s = 1; int x = s; (void)x; (void)q; }
