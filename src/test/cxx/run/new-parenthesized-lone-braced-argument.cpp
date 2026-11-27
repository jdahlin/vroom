// new T({a, b}): the braced-init-list is the one argument of T's constructor
// ([over.ics.list]), not T's own initializer.
struct A {
    A(int x, int y) : v(10 * x + y) {}
    A(A &&o) : v(o.v) {}
    int v;
};
struct RC {
    RC(A a) : v(a.v) {}
    int v;
};

template <typename T> static int make(T p)
{
    RC *r = new RC({p, 2});
    int v = r->v;
    delete r;
    return v;
}

int main()
{
    RC *r = new RC({3, 4});
    int v = r->v;
    delete r;
    return v + make(1) - 46;
}
