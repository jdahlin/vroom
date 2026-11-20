// [over.match.oper]/3, [over.match.best], [class.copy.assign]: the implicit
// copy and move assignment operators take a class object; a right operand of
// another type is converted to it (a converting constructor, or a derived
// class bound to the base's parameter), and explicit constructors do not
// take part.
struct Index {
    unsigned i = 0;
    constexpr Index() = default;
    constexpr Index(decltype(nullptr)) {}
    explicit constexpr Index(unsigned v) : i(v) {}
};
struct Wrap {
    Index n;
    void clear() { n = 0; }
    void set(unsigned v) { n = Index(v); }
};
struct Pair {
    int a, b;
    Pair(int x) : a(x), b(x + 1) {}
    Pair() : a(0), b(0) {}
};
struct Base {
    int v;
};
struct Derived : Base {
    int w;
};
int main()
{
    Wrap w;
    w.set(5);
    if (w.n.i != 5)
        return 1;
    w.clear();
    if (w.n.i != 0)
        return 2;
    Pair p;
    p = 7;
    if (p.a != 7 || p.b != 8)
        return 3;
    Base b = {1};
    Derived d;
    d.v = 9;
    d.w = 4;
    b = d;
    if (b.v != 9)
        return 4;
    return 0;
}
