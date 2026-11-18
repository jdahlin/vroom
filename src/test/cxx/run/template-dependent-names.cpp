/* Dependent names in templates ([temp.res], [temp.dep]): typename T::name
   in a function template's declarator and body, nested (T::a::b), and
   C++20's implicit typename; T::value and T::f() in bodies; a class
   template's member of type typename T::type; names of a dependent base
   not found by unqualified lookup ([temp.dep]/3): the global ones are, and
   this->x and T::f() reach the base. */
extern "C" int printf(const char *, ...);

struct A {
    struct type {
        int v;
    };
    enum { value = 3 };
    static int f() { return 4; }
    struct a {
        struct b {
            int w;
        };
    };
};

template <class T> int g(T) { return T::value + T::f(); }
template <class T> int g2(T)
{
    typename T::type x;
    x.v = 2;
    return x.v;
}
template <class T> typename T::type g3(T, typename T::type u)
{
    u.v += 1;
    return u;
}
template <class T>
typename T::a::b g4(typename T::a, typename T::a::b x, T)
{
    x.w *= 2;
    return x;
}
template <class T> T::type g5(const T &) { return {5}; }
template <class T> struct B {
    int h() { return T::value; }
    typename T::type m;
};

struct Base {
    int x;
    static int f() { return 1; }
};
int x = 7;
int f() { return 2; }
template <class T> struct D : T {
    int g() { return x; }
    int h() { return this->x; }
    int k() { return f(); }
    int q() { return T::f(); }
};

int main()
{
    A a;
    B<A> b;
    b.m.v = 5;
    A::type u;
    u.v = 6;
    A::a::b w;
    w.w = 7;
    A::a aa;
    D<Base> d;
    d.x = 3;
    int r = 0;
    if (g(a) != 7)
        r |= 1;
    if (b.h() != 3)
        r |= 2;
    if (g2(a) != 2)
        r |= 4;
    if (g3(a, u).v != 7)
        r |= 8;
    if (g4(aa, w, a).w != 14)
        r |= 16;
    if (b.m.v != 5)
        r |= 32;
    if (g5(a).v != 5)
        r |= 64;
    if (d.g() != 7)
        r |= 128;
    if (d.h() != 3)
        r |= 256;
    if (d.k() != 2)
        r |= 512;
    if (d.q() != 1)
        r |= 1024;
    printf("%d\n", r);
    return r;
}
