// [expr.unary.noexcept]: the noexcept operator on calls, constructions and
// operators
struct S {
    S(int) noexcept {}
    S(long) {}
    int f() noexcept { return 1; }
    int g() { return 2; }
};
int add(int a, int b) noexcept { return a + b; }
long sub(long a, long b) { return a - b; }
int (*fp)(int, int) noexcept = add;
static_assert(noexcept(add(1, 2)), "");
static_assert(!noexcept(sub(1, 2)), "");
static_assert(noexcept(fp(1, 2)), "");
static_assert(noexcept(1 + 2 * 3), "");
static_assert(noexcept(S(1)), "");
static_assert(!noexcept(S(1L)), "");
static_assert(noexcept(S(1).f()), "");
static_assert(!noexcept(S(1).g()), "");
static_assert(!noexcept(add(1, 2) + sub(3, 4)), "");
int main()
{
    bool a = noexcept(add(1, 2));
    bool b = noexcept(sub(1, 2));
    return a && !b && add(1, 2) == 3 ? 0 : 1;
}
