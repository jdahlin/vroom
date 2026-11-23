// Names and scopes that are well-formed ([basic.lookup], [basic.scope],
// [namespace.def], [namespace.udir], [namespace.udecl], [namespace.alias],
// [basic.link]/6): bench/cxx-sema.py has clang verify the types of the
// expressions, the declared types and the symbols.
namespace A {
int a = 1;
long la = 2;
namespace B {
char b = 'b';
int f() { return a + b; }
}
inline namespace I {
short s = 3;
}
int g() { return s + B::b + B::f(); }
struct S { int m; static int sm; };
enum E { e1, e2 };
}
int A::S::sm = A::a + A::e2;
namespace A {
extern int later;
int h();
}
int A::later = a + la;
int A::h() { return later + I::s + ::A::B::b; }
namespace { int hidden = 4; }
int useHidden() { return hidden + ::hidden; }
namespace N1 { int x = 1; long y = 2; }
namespace N2 { using namespace N1; int z = x; }
int viaDirective() { return N2::x + N2::z; }
namespace N3 { using N1::y; int w = y; }
namespace Alias = A::B;
int viaAlias() { return Alias::b; }
using namespace N3;
long seen = y;
struct stat { int size; };
int stat(struct stat *p) { return p->size; }
struct stat st;
int callStat() { return stat(&st); }
namespace ADL {
struct T { int v; };
int take(T t) { return t.v; }
enum K { k1 };
int kind(K) { return 0; }
}
int argumentDependent(ADL::T t) { return take(t) + kind(ADL::k1); }
int blockDeclarations() {
  extern int defined_later;
  int helper(int);
  using A::a;
  using namespace A::B;
  namespace L = A;
  return helper(defined_later) + a + b + L::la;
}
int defined_later = 5;
int helper(int v) { return v; }
int shadow = 1;
namespace Inner {
int shadow = 2;
int innermost() { int shadow = 3; return shadow + Inner::shadow + ::shadow; }
}
struct Outer { struct In; struct In *p; };
struct Outer::In { int i; };
int nested(Outer::In *q) { return q->i; }
