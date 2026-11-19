/* [dcl.constexpr]/2: consteval declares an immediate function;
   [expr.const]/13: each invocation outside an immediate function context
   is a constant expression, its value used in place of a call (no symbol
   for the function, as clang); decltype's operand is unevaluated */
struct tag {
    int x;
};

consteval int square(int n)
{
    return n * n;
}

/* an immediate function context: the call is not an immediate invocation */
consteval int sum_of_squares(int a, int b)
{
    return square(a) + square(b);
}

template <class T> consteval auto kind()
{
    return T{sizeof(T)};
}

template <class T> consteval T twice(T v)
{
    return v + v;
}

constexpr int nine = square(3);
static_assert(sum_of_squares(1, 2) == 5);

int main()
{
    int run = square(4) + twice(3L) + kind<char>();
    decltype(kind<tag>()) t = {7};
    static_assert(twice(5) == 10);
    return run + nine + t.x - 16 - 6 - 1 - 9 - 7;
}
