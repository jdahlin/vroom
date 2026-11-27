/* [expr.static.cast]/4: a static_cast to a reference of a class with an
   (explicit) conversion function ([over.match.ref]) */
struct M {
    int a, b;
    int sum() const { return a + b; }
};

template <typename T> struct Space {
    T data;
    explicit operator const T &() const { return data; }
};

struct Holder {
    M m;
    explicit operator M &() { return m; }
};

struct ByValue {
    int v;
    explicit operator M() const { return M{v, v + 1}; }
    explicit operator int() const { return v; }
};

struct OnlyM {
    int v;
    explicit operator M() const { return M{v, v + 1}; }
};

struct Number {
    int n = 7;
    operator const int &() const { return n; }
};

template <typename T> static int through(const Space<T> &s)
{
    return static_cast<const T &>(s).sum();
}

int main()
{
    Space<M> s{{1, 2}};
    if (through(s) != 3)
        return 1;
    const M &r = static_cast<const M &>(s);
    if (&r != &s.data)
        return 2;
    if (&(const M &)s != &s.data)
        return 3;
    Holder h{{3, 4}};
    static_cast<M &>(h).a = 10;
    if (h.m.a != 10)
        return 4;
    ByValue v{5};
    if (static_cast<const M &>(v).sum() != 11)
        return 5;
    if (static_cast<M &&>(OnlyM{1}).sum() != 3)
        return 6;
    if (static_cast<const int &>(v) != 5)
        return 7;
    Number n;
    if (&static_cast<const int &>(n) != &n.n)
        return 8;
    Space<int> si{9};
    if (&static_cast<const int &>(si) != &si.data)
        return 9;
    if (static_cast<const M &>(Space<M>{{20, 22}}).sum() != 42)
        return 10;
    return 0;
}
