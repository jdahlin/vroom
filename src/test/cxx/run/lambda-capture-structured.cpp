/* [expr.prim.lambda.capture]/4 (C++20): a structured binding is a local
   entity that can be captured */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

struct P { int x; int y; };

int main()
{
    P p{3, 4};
    auto [a, b] = p;
    auto f = [a, b] { return a + b; };
    auto g = [=] { return a * b; };
    auto h = [&] { a += 1; return a; };
    check(f() == 7 && g() == 12 && h() == 4);
    int arr[2] = {5, 6};
    auto [c, d] = arr;
    auto k = [&c, d] { c += d; return c; };
    check(k() == 11);
    auto nested = [&] { return [=] { return a + b; }(); };
    check(nested() == 8);
    auto mut = [a]() mutable { a += 1; return a; };
    check(mut() == 5 && mut() == 6 && a == 4);
    return 0;
}
