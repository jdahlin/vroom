int *const a; int *const *b; char *volatile *const c; int (*const fp)(void); int *restrict const r;
int x = a; int y = b; int z = c; int w = fp; int v = r;
