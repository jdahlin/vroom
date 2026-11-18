// [temp.mem]/1: member function templates defined in a class of namespace
// scope (libc++'s __equal_to in <__algorithm/comp.h>): called through
// x.f(args), p->f(args) and, for operator(), x(args), their template
// arguments deduced from the call's arguments; const ones, ones of a base
// class, one called from another member's body.
struct EqualTo {
    template <class T1, class T2>
    __attribute__((__visibility__("hidden"))) constexpr bool
    operator()(const T1 &x, const T2 &y) const
    {
        return x == y;
    }
};

struct Counter {
    int base;
    template <class T> T add(T v) { return v + static_cast<T>(base); }
    template <class T, class U> int pair(T a, U *b) const
    {
        return static_cast<int>(a) + *b + base;
    }
    int twice(int v) { return add(v) + this->add(v); }
};

struct Derived : Counter {
    int extra;
};

struct Less {
    template <class T> bool operator()(const T &a, const T &b) const
    {
        return a < b;
    }
};

template <class Compare> int count_less(Compare cmp, const int *v, int n)
{
    int k = 0;
    for (int i = 0; i + 1 < n; i++)
        if (cmp(v[i], v[i + 1]))
            k++;
    return k;
}

int main()
{
    // (operands that are no constants: clang folds a constant call)
    int one = 1, two = 2;
    long one_l = 1;
    double three = 3.0;
    char a = 'a';
    EqualTo eq;
    if (!eq(one, one_l) || eq(two, three))
        return 1;
    const EqualTo ceq{};
    if (!ceq(a, 97))
        return 2;
    Counter c{10};
    if (c.add(5) != 15 || c.add(2.5) != 12.5)
        return 3;
    int seven = 7;
    const Counter *p = &c;
    if (p->pair(1L, &seven) != 18)
        return 4;
    Derived d;
    d.base = 1;
    d.extra = 0;
    if (d.add(4) != 5)
        return 5;
    if (c.twice(1) != 22)
        return 6;
    int v[] = {1, 3, 2, 5, 4};
    if (count_less(Less(), v, 5) != 2)
        return 7;
    return 0;
}
