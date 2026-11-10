/* diag-flags: -Wall */
/* runtime warnings at the end of the function (after its other diagnostics); unused variables after a DefaultError conversion error */
int g(int);
int f(int x){ int r = x << (-1); r += x << 64; int *q = x; r += x / 0; return r; }
int f2(int x){ int *q = x; int r; return 0; }
int g(int x){ int *q = x; return 0; }
