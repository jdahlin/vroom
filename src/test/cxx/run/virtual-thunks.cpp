/* [class.virtual]/2 across virtual bases, [abi:mangling-special-thunks]:
   overriders outside a virtual base called through virtual thunks that
   read the vcall offset ([abi:vtable-components]); a virtual destructor
   in a virtual base (D1/D0 virtual thunks); an overrider inherited from
   one side of a diamond */
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;
void *operator new(size_t n) { return malloc(n); }
void operator delete(void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }

int dtors;
struct A { int a; A() : a(1) {} virtual int f() { return a; } virtual int g() { return 2; } virtual ~A() { dtors += 1; } };
struct B : virtual A { int b; B() : b(3) {} int f() override { return a + b; } ~B() { dtors += 10; } };
struct C : B { int c; C() : c(4) {} int g() override { return c * 10; } };
struct L : virtual A { int l; L() : l(7) {} int g() override { return l + a; } };
struct R : virtual A { int r; R() : r(8) {} };
struct D : L, R { int d; D() : d(9) {} };
struct E : B, L { int e; E() : e(5) {} ~E() { dtors += 100; } };

struct X { int x; virtual ~X() { dtors += 1000; } };
struct Y { int y; virtual ~Y() { dtors += 10000; } };
struct Z : X, Y { ~Z() { dtors += 100000; } };

int use(A *p) { return p->f() * 100 + p->g(); }

int main() {
  C c;
  D d;
  E e;
  B b;
  printf("%d %d %d %d\n", use(&c), use(&d), use(&e), use(&b));
  A *p = new C;
  printf("%d\n", use(p));
  delete p;
  printf("%d\n", dtors);
  p = new E;
  delete p;
  printf("%d\n", dtors);
  Y *y = new Z;
  delete y;
  printf("%d\n", dtors);
  R *r = &d;
  printf("%d\n", r->g());
  return 0;
}
