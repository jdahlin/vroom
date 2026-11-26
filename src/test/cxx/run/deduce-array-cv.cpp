/* [temp.deduct.call]/4.1: a reference parameter's array of more
   cv-qualified elements; B b({}) ([dcl.init]/17.6.2); an explicit
   conversion function binding the copy constructor's parameter in
   direct-initialization ([over.match.ref]/1.1) */
typedef __SIZE_TYPE__ size_t;

template <class T> struct Span {
    const T *p;
    size_t n;
    template <size_t N> Span(T (&a)[N]) : p(a), n(N) {}
};
struct P {
    int x;
};
template <int N> int bound(const int (&a)[N]) { return a[0] + N; }
static int first(Span<const P> s) { return s.p[0].x + static_cast<int>(s.n); }

struct Q {
    int v = 5;
};
struct B {
    int v;
    explicit B(const Q &q) : v(q.v) {}
};

struct V {
    int x;
    V() : x(0) {}
    explicit V(const P &p) : x(p.x) {}
};
struct L {
    V d;
    explicit operator const V &() const { return d; }
};

int main()
{
    P a[3] = {{4}, {5}, {6}};
    int i[2] = {7, 8};
    if (first(a) != 7 || bound(i) != 9)
        return 1;
    B b({});
    if (b.v != 5)
        return 2;
    L l;
    l.d.x = 11;
    if (V(l).x != 11 || static_cast<V>(l).x != 11)
        return 3;
    return 0;
}
