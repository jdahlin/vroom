/* -Wint-conversion: "; take the address with &" and "; dereference with *" (parenthesized operands too) */
struct S { int a; long b; } s;
int g; int arr[3];
void take(int *);
int *f(int x, struct S *ps){
  int *q = x; long *r = s.b; int *t = arr[1]; int *u = ps->a; take(g); q = x;
  int *v = (x); short *w = x; int *z = x + 1;
  return x;
}
int *p; struct T { int *m; } t;
int f(void){ int y = (p); int z = (t.m); y = p; return (p); }
