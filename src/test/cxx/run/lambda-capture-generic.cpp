/* [expr.prim.lambda.capture] in generic lambdas ([expr.prim.lambda.closure]/3):
   auto parameters and template-heads capturing */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int main()
{
    int base = 10;
    auto add = [=](auto x) { return x + base; };
    check(add(1) == 11 && add(2.5) == 12.5);
    auto by_ref = [&](auto x) { base += x; return base; };
    check(by_ref(1) == 11 && base == 11);
    auto th = [base]<class T>(T x) { return x * base; };
    check(th(2) == 22);
    auto init = [m = base + 1](auto x) { return x + m; };
    check(init(1) == 13);
    auto nested = [=](auto x) { return [=](auto y) { return x + y + base; }(1); };
    check(nested(2) == 14);
    /* a constant odr-used in the innermost of nested generic lambdas, and
       a parameter of the outermost: both captured down the chain */
    const int k = 10;
    auto outer = [=](auto a) {
        return [=](auto b) {
            return [=](auto c) {
                const int &r = k;
                decltype(a) a2 = a;
                return r + a2 + b + c;
            };
        };
    };
    auto m1 = outer(1);
    auto m2 = m1(2);
    check(sizeof(outer) == sizeof(int) && sizeof(m1) == 2 * sizeof(int) &&
          sizeof(m2) == 3 * sizeof(int));
    check(m2(3) == 16);
    return 0;
}
