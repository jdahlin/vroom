/* [expr.type.conv]/1: a decltype-specifier is a simple-type-specifier, so
   decltype(e)(args) and decltype(e){args} convert to the type it denotes
   (doctest's decltype(filters)(9)) */
extern "C" int printf(const char *, ...);

struct box {
    int n, m;
    box(int a) : n(a), m(0)
    {
    }
    box(int a, int b) : n(a), m(b)
    {
    }
};

struct plain {
    int a, b;
};

template <class T> struct many {
    T value;
    int count;
    explicit many(int n) : value(), count(n)
    {
    }
};

many<many<int> *> filters(0);

template <class T> int both(T x)
{
    return decltype(x)(x + 1) + decltype(x){x};
}

int main()
{
    box b(1);
    box c = decltype(b)(2);
    box d = decltype(b){3, 4};
    box e = decltype(b)(5, 6);
    plain p{1, 2};
    p = decltype(p){7, 8};
    long l = 0;
    int x = 300;
    char ch = 0;
    filters = decltype(filters)(9);
    printf("%d %d %d %d %d\n", c.n, d.n + d.m, e.n + e.m, p.a, p.b);
    printf("%d %ld %d\n", filters.count, decltype(l)(x), decltype(ch)(x));
    printf("%d %d\n", decltype(x)(), both(4));
    (decltype((x))(x)) = 5;
    printf("%d\n", x);
}
