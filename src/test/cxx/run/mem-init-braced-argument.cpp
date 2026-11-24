/* [class.base.init]/7, [over.match.ctor], [over.ics.list]: a braced-init-list
   as an argument of a mem-initializer or of a delegating constructor */
struct R {
    int a;
};
struct L {
    int n;
    L() : n(1) {}
};
struct P {
    int v;
    P(int x, R r) : v(x + r.a) {}
    P(int x) : P(x, {}) {}
    P(int x, int y, int z) : P(x + z, {y}) {}
};
struct Q {
    int v;
    Q(int x, R r, const L &l) : v(x + r.a + l.n) {}
};
struct D : Q {
    D() : Q(5, {7}, {}) {}
};
struct M {
    Q q;
    M() : q(1, {2}, {}) {}
};
int main()
{
    P p(3);
    P p2(3, 4, 1);
    D d;
    M m;
    if (p.v != 3 || p2.v != 8 || d.v != 13 || m.q.v != 4)
        return 1;
    return 0;
}
