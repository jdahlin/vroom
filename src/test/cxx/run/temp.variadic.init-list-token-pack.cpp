// [temp.variadic]: pack expansion of a single identifier token in a
// braced-init-list (e.g. `vargs<T...>{{args...}}` in fmt/format.h:4200).

struct Val {
    int v;
    constexpr Val(int x) : v(x) {}
};

template <int N> struct Store {
    Val data[N];
};

template <class... T> int sum_args(T &&...args)
{
    Store<sizeof...(T)> s{{args...}};
    int total = 0;
    for (int i = 0; i < (int)sizeof...(T); i++)
        total += s.data[i].v;
    return total;
}

int main()
{
    if (sum_args(10, 20, 12) != 42)
        return 1;
    if (sum_args(7) != 7)
        return 2;
    return 0;
}
