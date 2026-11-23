/* [dcl.spec.auto]/3: a non-member operator function whose return type is
   deduced from its body; [over.match.oper], [class.spaceship] */
struct D {
    int v;
};

auto operator+(const D &a, const D &b)
{
    return a.v + b.v;
}

auto operator==(const D &a, const D &b)
{
    return a.v == b.v;
}

template <class T> auto operator-(const T &a, const D &b)
{
    return a.v - b.v;
}

int main()
{
    D a{5}, b{3};
    if ((a + b) != 8)
        return 1;
    if (a == b)
        return 2;
    if (!(a != b))
        return 4;
    if ((a - b) != 2)
        return 3;
    return 0;
}
