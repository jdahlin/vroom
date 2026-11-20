/* [expr.prim.lambda.closure]: conversion to function pointer, constexpr
   and consteval, noexcept, trailing return types, copy and move, default
   construction of captureless closures (C++20), closure types as template
   arguments and in decltype */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

template <class F> int call(F f) { return f(2); }

constexpr auto sq = [](int x) { return x * x; };
static_assert(sq(4) == 16);
static_assert([](int x) consteval { return x + 1; }(2) == 3);
static_assert([] { return 5; }() == 5);
static_assert(noexcept([]() noexcept {}()));
static_assert(!noexcept([] {}()));

int zero = 0;

int main()
{
    int (*fp)(int) = [](int x) { return x + 1; };
    check(fp(1) == 2);
    int (*fn)(int) noexcept = [](int x) noexcept { return x + 2; };
    check(fn(1) == 3);
    auto t = [](int x) -> long { return x; };
    check(sizeof(t(1)) == sizeof(long));
    int k = 5;
    auto c = [k](int x) { return x + k; };
    auto c2 = c;
    auto c3 = static_cast<decltype(c) &&>(c2);
    check(c3(1) == 6);
    using L = decltype([](int x) { return x * x; });
    L s2, s3;
    s2 = s3;
    check(s2(3 + zero) == 9);
    check(call(c) == 7);
    return 0;
}
