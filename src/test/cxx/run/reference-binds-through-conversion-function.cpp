/* [dcl.init.ref]/5.1.2, [over.match.ref]: a reference to a class binds
   directly to the lvalue a conversion function of the argument yields; the
   implicit copy of a class that declares such a conversion function and a
   converting constructor takes the copy constructor */
struct S {
    int v;
    S(int x) : v(x) {}
    S(const S &o) : v(o.v + 100) {}
};

struct Holder {
    S s;
    Holder(const S &x) : s(x) {}
    operator const S &() const { return s; }
};

struct Outer {
    Holder h;
};

int take(const S &x)
{
    return x.v;
}

int take(long)
{
    return -1;
}

int main()
{
    Holder h(S(5));
    int direct = take(h);
    if (direct != 105)
        return 1;
    Outer a = {h};
    Outer b(a);
    if (b.h.s.v != 305)
        return 2;
    return 0;
}
