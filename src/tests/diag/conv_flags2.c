/* diag-flags: -Wno-incompatible-pointer-types -Werror=incompatible-function-pointer-types */
/* -Werror= of a default error: clang's plain error */
void f(int *p);
void g(void) { long l; char *s = 0; int (*fp)(void) = 0; void (*q)(int) = fp; f(&l); s = 1; int x = s; (void)x; (void)q; }
