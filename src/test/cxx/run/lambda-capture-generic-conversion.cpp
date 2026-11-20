/* [expr.prim.lambda.closure]/8: a generic lambda without a lambda-capture
   converts to a pointer to function, by a conversion function template
   whose template-arguments are deduced from the target type */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int zero = 0;

int main()
{
    auto g = [](auto x) { return x + 1; };
    int (*gp)(int) = g;
    check(gp(1 + zero) == 2);
    double (*gd)(double) = g;
    check(gd(1.5) == 2.5);
    return 0;
}
