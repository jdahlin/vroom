/* [class.derived]: a base written as a decltype-specifier (libc++'s __is_callable) */
struct yes { static const int value = 1; };
struct no { static const int value = 2; };
template <class T> T &&dv();
template <class F, class = decltype(dv<F>()())> yes helper(int);
template <class> no helper(...);
template <class F> struct callable : decltype(helper<F>(0)) {};
struct fn { void operator()(); };
struct direct : decltype(yes()) {};
static_assert(callable<fn>::value == 1, "");
static_assert(callable<int>::value == 2, "");
static_assert(direct::value == 1, "");
int main() { return !(callable<fn>::value == 1 && callable<int>::value == 2); }
