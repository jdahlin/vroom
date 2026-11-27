// [temp.variadic]/10 over operands of class and enumeration type:
// conversion functions and operator functions ([over.match.oper])
struct z {
    float f;
    operator float() const { return f; }
};

struct m {
    int v;
};
static m operator-(m a, m b) { return m{a.v * 10 - b.v}; }

enum e { one = 1, two = 2, four = 4 };
static e operator|(e a, e b) { return e(int(a) | int(b)); }

template <class T, class... P> float product(T x, P... values)
{
    T prod = x - x + 1;
    prod = (prod * ... * values);
    return prod;
}

template <class... P> int left(P... p) { return (... - p).v; }
template <class... P> int right(P... p) { return (p - ...).v; }
template <class... P> int all(P... p) { return (p | ...); }
template <class... P> int sum(P... p) { return (0 + ... + p); }

int main()
{
    __builtin_printf("%g\n", double(product(2.0f, z{3}, 5.0f, z{7})));
    __builtin_printf("%d %d\n", left(m{1}, m{2}, m{3}), right(m{1}, m{2}, m{3}));
    __builtin_printf("%d %d\n", all(one, two, four), sum(z{1.5f}, z{2.5f}, two));
    return 0;
}
