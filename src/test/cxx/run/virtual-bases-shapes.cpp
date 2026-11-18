/* [class.mi]/4-6, [abi:vtable-ctor-vtt]: virtual bases in more shapes:
   a base without virtual functions, a chain under the diamond, a virtual
   base with its own virtual base, mixed virtual and non-virtual bases,
   destructors, copies and member access through each path */
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;
void *operator new(size_t n) { return malloc(n); }
void operator delete(void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }

int order[32];
int done;
void note(int x) { order[done++] = x; }

struct P { int p; P() : p(5) { note(1); } ~P() { note(-1); } };
struct Q : virtual P { int q; Q() : q(6) { note(2); } virtual ~Q() { note(-2); } virtual int f() { return q + p; } };
struct R : virtual P { int r; R() : r(7) { note(3); } ~R() { note(-3); } };
struct S : Q, R { int s; S() : s(8) { note(4); } ~S() { note(-4); } int f() { return s + q + r + p; } };
struct T : S { int t; T() : t(9) { note(5); } ~T() { note(-5); } };

struct V0 { int v0; V0() : v0(10) {} virtual int g() { return v0; } };
struct V1 : virtual V0 { int v1; V1() : v1(11) {} virtual int h() { return v1 + v0; } };
struct V2 : virtual V1 { int v2; V2() : v2(12) {} virtual int k() { return v2 + v1 + v0; } };
struct V3 : V2, virtual V0 { int v3; V3() : v3(13) {} };

struct N { int n; virtual int m() { return n; } N() : n(14) {} };
struct M : N, virtual P { int mm; M() : mm(15) {} int m() { return mm + p + n; } };

int byP(P *x) { return x->p; }
int byQ(Q *x) { return x->f(); }
int byR(R &x) { return x.r + x.p; }

int main() {
  {
    T t;
    printf("%d %d %d %d\n", t.f(), byQ(&t), byR(t), byP(&t));
    t.p = 50;
    S copy = t;
    printf("%d %d\n", copy.f(), byP(&copy));
  }
  for (int i = 0; i < done; i++)
    printf("%d ", order[i]);
  printf("\n");
  V3 v;
  V1 *v1 = &v;
  V0 *v0 = &v;
  printf("%d %d %d %d %d\n", v.k(), v1->h(), v0->g(), v.v0, v.v1);
  M m;
  N *n = &m;
  printf("%d %d %d\n", n->m(), byP(&m), m.p);
  Q *q = new S;
  int f = q->f();
  delete q;
  printf("%d %d\n", f, done);
  return 0;
}
