// [namespace.udecl]/1, /15: using-declarations of the same member name from
// two base classes make one overload set; the class's own functions hide
// the bases' with their signature; a base's own set is not changed by it
struct A {
    int f(int x) { return x + 1; }
    int f(long x) { return static_cast<int>(x) + 2; }
    int g(int) { return 1; }
};
struct B {
    int f(char *p) { return p ? 10 : 20; }
    int f(double d) { return static_cast<int>(d) + 30; }
    int h(int) { return 4; }
};
struct C {
    int f(short) { return 99; }
};
struct D : A, B {
    using A::f;
    using B::f;
};
struct E : A, B {
    using A::f;
    int f(int x) { return x * 5; }
    using B::f;
};
struct F : A, B, C {
    using A::f;
    using B::f;
    using C::f;
    int f(const char *) { return 77; }
};
struct G : A, B {
    using A::f;
    using B::f;
    using A::g;
    using B::h;
};
int main()
{
    char c;
    D d;
    if (d.f(1) != 2 || d.f(1L) != 3 || d.f(&c) != 10 || d.f(2.0) != 32)
        return 1;
    E e;
    if (e.f(1) != 5 || e.f(1L) != 3 || e.f(&c) != 10 || e.f(2.0) != 32)
        return 2;
    F f;
    if (f.f(1) != 2 || f.f(&c) != 10 || f.f(static_cast<short>(1)) != 99 ||
        f.f("x") != 77)
        return 3;
    G g;
    if (g.f(1) != 2 || g.f(&c) != 10 || g.g(1) != 1 || g.h(1) != 4)
        return 4;
    // the bases' own sets are as they were
    A a;
    B b;
    if (a.f(1) != 2 || a.f(1L) != 3 || b.f(&c) != 10 || b.f(2.0) != 32)
        return 5;
    return 0;
}
