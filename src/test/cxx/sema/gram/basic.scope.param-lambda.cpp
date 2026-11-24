// [basic.scope.param]/1: "A function parameter (including one appearing in
// a lambda-declarator) ... has function parameter scope": a lambda's later
// parameters and its trailing-return-type find an earlier parameter.
void f()
{
    auto a = [](int n, decltype(n) m) { return n + m; };
    auto b = [](auto x, decltype(x) y) { return x; };
    auto c = [](int n) -> decltype(n) { return n; };
    auto d = [](int n, int m) -> decltype(n + m) { return n + m; };
}
