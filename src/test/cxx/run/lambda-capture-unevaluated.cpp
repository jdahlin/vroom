/* lambdas in unevaluated operands (C++20), default arguments, default
   member initializers, static members */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

template <class T> using alias = decltype([](T x) { return x; });

int f(int x = [] { return 7; }()) { return x; }

struct S {
    int a = [] { return 1; }();
    static inline int s = [] { return 2; }();
    int g(int d = [] { return 3; }()) { return d; }
};

int main()
{
    check(sizeof([] { return 1; }) == 1);
    alias<int> l;
    check(l(5) == 5);
    check(f() == 7 && f(1) == 1);
    S s;
    check(s.a == 1 && S::s == 2 && s.g() == 3);
    int n = 4;
    check(sizeof([&] { return n; }(), 0) == sizeof(int));
    return 0;
}
