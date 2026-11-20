/* [expr.prim.lambda.capture]/17, [temp.variadic]: pack captures */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

template <int... Is> struct seq {};

template <class... Ts> int sum_copy(Ts... xs)
{
    return [xs...] { return (xs + ... + 0); }();
}
template <class... Ts> int sum_ref(Ts &...xs)
{
    return [&xs...] { return (xs + ... + 0); }();
}
template <class... Ts> int sum_init(Ts... xs)
{
    return [...ys = xs] { return (ys + ... + 0); }();
}
template <class... Ts> int sum_default(Ts... xs)
{
    return [=] { return (xs + ... + 0); }();
}
template <class... Ts> int sum_ref_init(Ts... xs)
{
    return [&...ys = xs] { return (ys + ... + 0); }();
}
template <class T> T load(const T *p)
{
    return [=]<int... Is>(seq<Is...>) { return (p[Is] + ... + T(0)); }(seq<0>{});
}
template <class It> int load4(It it)
{
    return [=]<int... Is>(seq<Is...>) noexcept { return (it[Is] + ... + 0); }(seq<0, 1, 2, 3>{});
}

int main()
{
    int a = 1, b = 2;
    check(sum_copy(1, 2, 3) == 6);
    check(sum_ref(a, b) == 3);
    check(sum_init(4, 5) == 9);
    check(sum_default(1, 2, 3, 4) == 10);
    check(sum_ref_init(a, b) == 3);
    int arr[4] = {1, 2, 3, 4};
    check(load4(arr) == 10);
    check(load(arr) == 1);
    return 0;
}
