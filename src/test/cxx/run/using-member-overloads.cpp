// [namespace.udecl]/15 (C++20 /14): member functions a using-declaration
// brings from a base class join the derived class's own functions of the
// name as overloads; an own function with the same parameter-type-list and
// cv-qualification hides the base's
struct B {
    int f(int x) { return x + 1; }
    int f(char *p) { return p ? 100 : 200; }
    int f(long) { return 300; }
    int g(int) { return 1; }
    int g(int) const { return 2; }
    static int s(int x) { return x + 5; }
    int v(int) { return 7; }
    virtual int w(int) { return 8; }
    virtual ~B() {}
};
// the own function after the using-declaration
struct D1 : B {
    using B::f;
    int f(double x) { return static_cast<int>(x) + 1000; }
};
// the own function before it
struct D2 : B {
    int f(double x) { return static_cast<int>(x) + 2000; }
    using B::f;
};
// the same signature hides the base's, after and before
struct D3 : B {
    using B::f;
    int f(int x) { return x * 2; }
    using B::g;
    int g(int) const { return 22; }
    using B::s;
    using B::v;
    int v(int, int) { return 70; }
    using B::w;
    int w(int) { return 80; }
};
struct D4 : B {
    int f(int x) { return x * 3; }
    using B::f;
    int v(int, int) { return 55; }
    using B::v;
};
// two levels: a using-declaration of what another one introduced
struct E : D1 {
    using D1::f;
    int f(short x) { return x + 3000; }
};
int main()
{
    char c;
    D1 d1;
    if (d1.f(5) != 6 || d1.f(&c) != 100 || d1.f(2.5) != 1002 || d1.f(7L) != 300)
        return 1;
    D2 d2;
    if (d2.f(5) != 6 || d2.f(&c) != 100 || d2.f(2.5) != 2002 || d2.f(7L) != 300)
        return 2;
    D3 d3;
    const D3 &cd3 = d3;
    if (d3.f(5) != 10 || d3.f(&c) != 100 || d3.f(7L) != 300)
        return 3;
    if (d3.g(1) != 1 || cd3.g(1) != 22 || static_cast<B &>(d3).f(5) != 6)
        return 4;
    if (D3::s(1) != 6 || d3.s(1) != 6 || d3.v(1) != 7 || d3.v(1, 2) != 70)
        return 5;
    B &b = d3;
    if (b.w(1) != 80 || d3.w(1) != 80)
        return 6;
    D4 d4;
    if (d4.f(5) != 15 || d4.f(&c) != 100 || d4.f(1L) != 300)
        return 7;
    if (d4.v(1) != 7 || d4.v(1, 2) != 55)
        return 8;
    E e;
    if (e.f(5) != 6 || e.f(&c) != 100 || e.f(2.5) != 1002 ||
        e.f(static_cast<short>(1)) != 3001)
        return 9;
    return 0;
}
