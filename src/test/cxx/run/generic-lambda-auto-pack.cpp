/* [dcl.fct]/22, [expr.prim.lambda.closure]/3: invented type template parameter
   pack from a generic parameter type placeholder (auto... / const auto&... /
   auto&&...) expanded during instantiation. */

int sum_values()
{
    return 0;
}

template <typename T, typename... Ts>
int sum_values(T first, Ts... rest)
{
    return first + sum_values(rest...);
}

int sum_abbrev(const auto &...args)
{
    return sum_values(args...);
}

int count_unnamed(int x, auto &&...)
{
    return x;
}

template <typename T, typename... Ts>
int apply_tail(T first, Ts... rest)
{
    auto tail_sum = [](auto &&, const auto &...args) {
        return sum_values(args...);
    };
    return tail_sum(first, rest...);
}

int main()
{
    auto direct = [](auto &&, const auto &...args) {
        return sum_values(args...);
    };
    if (direct(100, 1) != 1)
        return 1;
    if (direct(100, 1, 2, 3) != 6)
        return 2;
    if (apply_tail(100, 5) != 5)
        return 3;
    if (apply_tail(100, 10, 20, 30) != 60)
        return 4;
    if (sum_abbrev(4) != 4)
        return 5;
    if (sum_abbrev(4, 5, 6) != 15)
        return 6;
    int v = 42;
    auto ignore = [&v](auto &&...) { return v; };
    if (ignore() != 42)
        return 7;
    if (ignore(1, 2, 3) != 42)
        return 8;
    if (count_unnamed(7) != 7 || count_unnamed(7, 1, 2) != 7)
        return 9;
    return 0;
}
