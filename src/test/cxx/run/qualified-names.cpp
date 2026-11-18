// [basic.lookup.qual], [namespace.qual], [class.qual]: nested-name-specifiers
// of namespaces, classes and enumerations, with and without a leading ::, in
// expressions, type names, elaborated-type-specifiers, base-specifiers and
// mem-initializers; [dcl.meaning]/1, [namespace.memdef]/2: members of a
// namespace (and of its inline namespace set) defined outside it; [class.pre]/3:
// a class defined by a qualified class-head-name
namespace A {
int v = 3;
extern int later;
int f(int x);
namespace B {
struct C { int m; static int s; int get(); enum E { e1 = 4 }; };
struct Fwd;
int w = 4;
}
inline namespace I { int i1 = 5; extern int i2; }
enum G { g1 = 2 };
enum class H { h1 = 3 };
typedef B::C CT;
}
namespace { int u = 7; }
int A::B::C::s = A::v + 1;
int A::B::C::get() { return m + ::A::B::w + A::i1 + C::e1 + A::B::C::E::e1 + s; }
A::B::C c;
struct A::B::C *pc = &c;
::A::B::C::E e = A::B::C::e1;
int t = 1;
int A::later = v + 2;
int A::f(int x) { return x + v + later; }
int A::i2 = 6;
struct A::B::Fwd { int q; };
A::B::Fwd fwd = { 9 };
struct D : A::B::C { int d; D() : A::B::C(), d(A::g1) {} };
int g() { return ::u + A::g1 + (int)A::H::h1 + A::I::i1 + A::CT::s; }
int main()
{
    c.m = 1;
    if (c.get() != 1 + 4 + 5 + 4 + 4 + 4) return 1;
    if (A::f(1) != 1 + 3 + 5) return 2;
    if (A::i2 != 6 || fwd.q != 9 || e != 4 || t != 1) return 3;
    D d;
    if (d.d != 2) return 4;
    if (g() != 7 + 2 + 3 + 5 + 4) return 5;
    if (pc->m != 1) return 6;
    return 0;
}
