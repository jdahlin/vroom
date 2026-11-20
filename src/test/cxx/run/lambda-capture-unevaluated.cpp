/* lambdas in unevaluated operands (C++20), default arguments, default
   member initializers */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

using alias_int = decltype([](int x) { return x; });

int zero = 0;
int f(int x = [] { return 7; }()) { return x; }

struct S {
    int a = [] { return 1; }();
};

int main()
{
    check(sizeof([] { return 1; }) == 1);
    alias_int l;
    check(l(5 + zero) == 5);
    check(f() == 7 && f(1) == 1);
    S s;
    check(s.a == 1);
    int n = 4;
    check(sizeof([&] { return n; }(), 0) == sizeof(int));
    return 0;
}
