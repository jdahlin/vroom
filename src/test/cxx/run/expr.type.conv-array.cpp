/* [expr.type.conv]: a braced conversion to an array type named by a
   typedef-name (fmt's FMT_APPLY_VARIADIC) */
extern "C" int printf(const char *, ...);

template <class... T>
constexpr int sum(T... t)
{
    int n = 0;
    using unused = int[];
    (void)unused{0, (n += t, 0)...};
    return n;
}
static_assert(sum(1, 2) == 3);

int main()
{
    using ints = int[];
    int k = 0;
    (void)ints{k++, k++};
    return sum(k, 2) - 4;
}
