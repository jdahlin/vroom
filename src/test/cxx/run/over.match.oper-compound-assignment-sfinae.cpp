// [over.match.oper]: E1 op= E2 of a class E1 with no viable operator function
// is ill-formed (no built-in candidate takes a class left operand), so in an
// immediate context it is a deduction failure ([temp.deduct]/8)
template <class T> T &&declval_();

struct None {
    int a;
};
struct Plus {
    int sum;
    Plus &operator+=(int v)
    {
        sum += v;
        return *this;
    }
};
struct Converts {
    operator int() const
    {
        return 1;
    }
};

template <class S, class = decltype(declval_<S &>() += 1)> int has(int)
{
    return 1;
}
template <class S> int has(long)
{
    return 0;
}

int main()
{
    Plus p = {1};
    p += 2;
    if (has<None>(0) != 0)
        return 1;
    if (has<Plus>(0) != 1)
        return 2;
    if (has<Converts>(0) != 0)
        return 3;
    return p.sum - 3;
}
