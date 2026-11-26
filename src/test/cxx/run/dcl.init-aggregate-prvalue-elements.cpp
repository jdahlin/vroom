/* cxx-run-flags: -std=c++20 */
/* An aggregate's element initialized by a prvalue of its class is that object, never a copy of a temporary
   ([dcl.init]/17.6.1, [class.copy.elision]/1): a function's result, either operand of a conditional,
   a member function's result in a pack expansion; the elements after the last initializer are initialized
   too ([dcl.init.aggr]/5). */
extern "C" int printf(const char *, ...);

static int live, moved;

struct S {
    int v;
    S const *self;
    S() : v(0), self(this) { ++live; }
    S(int v) : v(v), self(this) { ++live; }
    S(S const &o) : v(o.v), self(this) { ++live; }
    ~S()
    {
        --live;
        if (self != this)
            ++moved;
    }
    S get() const { return S(*this); }
};

static S get(int v)
{
    return S(v);
}

struct array {
    S e[3];
};
struct pair {
    S x, y;
};
struct nested {
    int n;
    pair p;
    int m;
};
struct partly {
    S e[3];
    int n;
};

/* (no destructor: vcc's union destructors still destroy the member) */
struct T {
    int v;
    T const *self;
    T(int v) : v(v), self(this) {}
    T(T const &o) : v(o.v), self(this) {}
};
union either {
    int i;
    T t;
};
struct holder {
    either u;
};

static T made(int v)
{
    return T(v);
}

template <int... I> struct seq {};
template <int I> static S const &pick(S const &s)
{
    return s;
}
template <int... I> static array make(S const &s, seq<I...>)
{
    return {{pick<I>(s).get()...}};
}

static array returned()
{
    return {{get(1), get(2), get(3)}};
}

static int bad;

static void check(S const &s, int v)
{
    printf("%d %d\n", s.v, s.self == &s);
    if (s.v != v || s.self != &s)
        bad++;
}

int main()
{
    S source(7);
    bool yes = source.v == 7;

    {
        array a = returned();
        check(a.e[0], 1), check(a.e[2], 3);
        array b = make(source, seq<0, 1, 2>{});
        check(b.e[0], 7), check(b.e[2], 7);
        array l{{get(4), yes ? get(5) : S(source), !yes ? get(0) : S(6)}};
        check(l.e[0], 4), check(l.e[1], 5), check(l.e[2], 6);
        pair p = {get(8), get(9)};
        check(p.x, 8), check(p.y, 9);
        nested n = {1, {get(10), get(11)}, 2};
        check(n.p.x, 10), check(n.p.y, 11);
        partly q = {{get(12)}, 13};
        check(q.e[0], 12), check(q.e[1], 0), check(q.e[2], 0);
        S plain[2] = {get(14), get(15)};
        check(plain[0], 14), check(plain[1], 15);
        holder h = {{.t = made(16)}};
        printf("%d %d\n", h.u.t.v, h.u.t.self == &h.u.t);
        if (h.u.t.self != &h.u.t)
            bad++;
        printf("%d %d %d\n", n.n, n.m, q.n);
    }
    printf("%d %d %d\n", live, moved, bad);
    return live != 1 || moved || bad;
}
