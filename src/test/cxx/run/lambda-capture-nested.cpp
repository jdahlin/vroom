/* [expr.prim.lambda.capture]/14, /7: captures through nested lambdas,
   references, parameters */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int zero = 0;
int apply(int (*f)(int), int x) { return f(x); }

int main()
{
    int a = 1, b = 10;
    auto outer = [=](int p) {
        auto inner = [&](int q) { return a + b + p + q; };
        return inner(100);
    };
    check(outer(1000) == 1111);
    auto by_ref = [&] { return [&] { a += 1; return a; }(); };
    check(by_ref() == 2 && a == 2);
    auto inner_explicit = [a] { return [a] { return a * 2; }(); };
    check(inner_explicit() == 4);
    auto mid = [a]() mutable { return [&] { a += 5; return a; }(); };
    check(mid() == 7 && mid() == 12 && a == 2);
    int &ref = a;
    auto cr = [ref] { return ref; };
    auto rr = [&ref] { ref += 1; return ref; };
    check(cr() == 2 && rr() == 3 && a == 3);
    const int k = 4;
    auto kk = [=] { return k + 1; };
    check(kk() + zero == 5);
    auto fparam = [](int n) { return [n](int m) { return n * m; }; };
    check(fparam(3 + zero)(4) == 12);
    auto cp = [](int n) { return n + 1; };
    check(apply(cp, 1 + zero) == 2);
    return 0;
}
