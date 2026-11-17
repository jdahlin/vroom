// [namespace.udir], [namespace.alias], [namespace.udecl]
namespace A { int a = 1; int f() { return 10; } struct S { int m; }; namespace In { int deep = 2; } }
namespace B { int b = 3; using namespace A; int g() { return a + b; } }
namespace C { using A::a; using A::S; int h() { S s = { a }; return s.m; } }
namespace Alias = A::In;
namespace Alias = A::In;
namespace D { namespace Short = ::A; int i() { return Short::a + Alias::deep; } }
using namespace B;
int viaB() { return b + a; }           // a through B's using-directive
namespace E { int x = 5; }
namespace F { int x = 6; int y = 7; }
namespace G { using namespace E; int k() { return x; } }
int qual() { return B::a + C::a + B::b; }   // [namespace.qual]: through using-directives
struct Base { int v; int get() { return v; } protected: int p; };
struct Der : Base { using Base::v; using Base::get; int both() { return v + get(); } protected: using Base::p; };
int main()
{
    if (B::g() != 4 || C::h() != 1 || D::i() != 3 || viaB() != 4) return 1;
    if (G::k() != 5 || qual() != 5) return 2;
    {
        using namespace F;
        if (y != 7) return 3;
        using E::x;
        if (x != 5) return 4;
        namespace L = F;
        if (L::x != 6) return 5;
    }
    using A::f;
    if (f() != 10) return 6;
    Der d; d.v = 2;
    if (d.both() != 4) return 7;
    return 0;
}
