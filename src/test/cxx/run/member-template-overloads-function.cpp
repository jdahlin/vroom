// [temp.mem]/2 with [over.match.best]/2.4: a member function template and a
// non-template member function of the same name, in either order, are
// overloads: a call weighs both, the non-template one wins on a tie; also
// a template assignment next to the copy and move ones (libc++'s any),
// through a derived class, with default arguments, and called unqualified in
// a member function.
struct A {
    int f(int) { return 1; }
    template <class T> int f(T, T) { return 2; }
    template <class T> int f(T) { return 3; }
};

struct B {
    template <class T> int g(T) { return 10; }
    int g(int) { return 20; }
    int g(long, int = 0) { return 30; }
    int g(char) const { return 40; }
};

struct D : B {
    int use() { return g(1) + g(1.5); }
};

struct C {
    int last = 0;
    C() {}
    C(const C &) {}
    C(C &&) {}
    C &operator=(const C &)
    {
        last = 1;
        return *this;
    }
    C &operator=(C &&)
    {
        last = 2;
        return *this;
    }
    template <class T> C &operator=(T &&)
    {
        last = 3;
        return *this;
    }
    template <class T> int total(T v)
    {
        return static_cast<int>(v) + 2000;
    }
    int total(int v) { return v + 1000; }
};

template <class M> struct U {
    M value;
    int put(M) { return 1; }
    template <class R> int put(R r);
};

template <class M> template <class R> int U<M>::put(R r)
{
    return 2 + static_cast<int>(r) * 0;
}

int main()
{
    A a;
    if (a.f(1) != 1 || a.f(1.0, 2.0) != 2 || a.f(1.5) != 3 || a.f('c') != 3)
        return 1;
    B b;
    const B &cb = b;
    if (b.g(1) != 20 || b.g(1.5) != 10 || b.g(2L) != 30 || cb.g('a') != 40)
        return 2;
    D d;
    if (d.use() != 30 || d.g(7) != 20)
        return 3;
    C c, e;
    const C &ce = e;
    c = ce;
    if (c.last != 1)
        return 4;
    c = static_cast<C &&>(e);
    if (c.last != 2)
        return 5;
    c = 5;
    if (c.last != 3)
        return 6;
    if (c.total(1) != 1001 || c.total(1.5) != 2001 || c.total(2L) != 2002)
        return 7;
    U<int> u;
    if (u.put(1) != 1 || u.put(1.5) != 2)
        return 8;
    return 0;
}
