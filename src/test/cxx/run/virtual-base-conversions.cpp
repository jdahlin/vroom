/* [conv.ptr]/3, [class.mi]/4: a pointer converted to a base inside a
   virtual base: to the virtual base through its vbase offset, then the
   fixed offset of the base in it; a null pointer stays null */
#include <stdio.h>
struct X { int x; X() : x(1) {} };
struct Y { int y; Y() : y(2) {} };
struct V : X, Y { int v; V() : v(3) {} };
struct D : virtual V { int d; D() : d(4) {} };
struct E : D { int e; E() : e(5) {} };
int gy(Y *p) { return p ? p->y : -1; }
D *next(D *p, int *calls) { ++*calls; return p; }
int main() {
  E e;
  D *dp = &e;
  Y *y = dp;
  X *x = &e;
  D *n = 0;
  Y *ny = n;
  int calls = 0;
  Y *z = next(dp, &calls);
  printf("%d %d %d %d %d %d\n", y->y, x->x, gy(&e), ny == 0, z->y, calls);
  printf("%d\n", gy(n));
  return 0;
}
