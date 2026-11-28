/* [expr.const]/5, [expr.call]/12: a constexpr function with an ellipsis is
   evaluated with arguments beyond its parameters */
static int made;
template <class F> constexpr int pick(F &x, ...) { return x + 1; }
constexpr int three(int a, int b, ...) { return a * 10 + b; }
constexpr int run()
{
    int a = 1;
    int n = 0;
    int p = pick(a, ++n, 3);
    return pick(a, true) + p + pick(a) + n * 100 +
           three(1, 2, 3, 4);
}
static_assert(run() == 118);
int main()
{
    constexpr int k = run();
    made = k;
    return made != 118;
}
