// [expr.prim.lambda.closure]/11: the conversion function is constexpr
extern "C" int printf(const char *, ...);
using F = int (*)(int);
struct P {
    int mode;
    F one;
    F two;
};
constexpr P table[] = {
    {1, [](int a) { return a + 1; }, [](int a) { return a * 3; }},
    {2, nullptr, F([](int a) { return a - 1; })},
};
constexpr F single = [](int a) { return a ^ 5; };
static_assert(table[0].mode == 1);
int main()
{
    int a = table[0].one(1), b = table[0].two(2), c = table[1].two(9);
    printf("%d %d %d %d\n", a, b, c, single(1));
    if (a != 2 || b != 6 || c != 8 || single(1) != 4)
        return 1;
    return table[1].one != nullptr;
}
