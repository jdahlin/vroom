/* [expr.prim.lambda]: lambdas at namespace scope, captureless
   ([expr.prim.lambda.capture]/3), the closure type in the namespace
   ([expr.prim.lambda.closure]/2) */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

int zero = 0;
auto add = [](int a, int b) { return a + b; };
inline auto mul = [](int a, int b) { return a * b; };
namespace n {
static auto neg = [](int a) -> long { return -a; };
}
auto counter = [n = 1]() mutable { return n++; };

int main()
{
    check(add(2, zero + 3) == 5);
    check(mul(4, zero + 5) == 20);
    check(n::neg(zero + 7) == -7);
    int (*fp)(int, int) = add;
    check(fp(1, zero + 2) == 3);
    check(counter() == 1 && counter() == 2);
    return 0;
}
