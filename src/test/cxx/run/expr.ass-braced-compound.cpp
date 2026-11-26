/* [expr.ass]/8: a braced-init-list on the right of a compound assignment: the
   argument of the class's operator function (member or not), and of a scalar
   the left operand's type initialized by it */
extern "C" int printf(const char *, ...);
namespace std
{
template <class T> class initializer_list
{
    const T *p;
    decltype(sizeof 0) n;

public:
    constexpr const T *begin() const
    {
        return p;
    }
    constexpr const T *end() const
    {
        return p + n;
    }
};
} // namespace std

struct list {
    int sum, calls;
    list &operator+=(std::initializer_list<int> v)
    {
        for (int x : v)
            sum += x;
        calls += 100;
        return *this;
    }
    list &operator+=(const list &other)
    {
        sum += other.sum;
        calls += 1;
        return *this;
    }
};

struct pair {
    int a, b;
};

pair &operator-=(pair &x, pair y)
{
    x.a -= y.a;
    x.b -= y.b;
    return x;
}

template <class T> T grow(T value)
{
    value += {2};
    return value;
}

int main()
{
    list l{0, 0};
    l += {1, 2, 3};
    l += {4};
    (l += {5, 6}) += {7};
    printf("%d %d\n", l.sum, l.calls);

    pair p{10, 20};
    p -= {1, 2};
    p -= {3};
    printf("%d %d\n", p.a, p.b);

    int i = 5;
    i += {3};
    i *= {2};
    i -= {};
    i <<= {1};
    long q = 1;
    q += {i};
    char c = 'a';
    c += {1};
    double d = 1;
    d /= {4.0};
    printf("%d %ld %c %g\n", i, q, c, d);
    printf("%d %g\n", grow(1), grow(1.5));
}
