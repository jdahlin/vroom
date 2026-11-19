/* cxx-run-flags: -std=c++20 */
/* [dcl.spec.auto]/1, [dcl.type.auto.deduct]/4-6: a return type or
   variable type C auto is deduced as auto and C<deduced> must be satisfied */
template <class T> concept Word = sizeof(T) == 4;
namespace ns {
template <class T> concept Small = sizeof(T) <= 4;
template <class T> Small auto half(T t)
{
    return t / 2;
}
} // namespace ns

template <class T> Word auto twice(T t)
{
    return t + t;
}

struct counter {
    int n;
    template <class U> constexpr Word auto add(U u) const
    {
        return n + u;
    }
};

Word auto global = 7;

int main()
{
    counter c{5};
    Word auto local = c.add(3);
    if (twice(4) != 8 || c.add(2) != 7 || local != 8 || global != 7 ||
        ns::half(8) != 4)
        return 1;
    return 0;
}
