/* -Wconditional-type-mismatch, "*(" ")" dereference hints, invalid operands of a pointer comparison */
int *f(int x, int *p, long l){ int *a = x ? p : x; int *b = x ? l : p; int *c = x ? p : 0; return x ? p : 1; }
int *p, *q; int x;
int f2(void){ int y = x ? p : q; int z = (x ? p : q); int w = p + 1; return x ? p : q; }
int f3(int *p, double d, float fl){ return (p == 0.5) + (d < p) + (p != fl); }

