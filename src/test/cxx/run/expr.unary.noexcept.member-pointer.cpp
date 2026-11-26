// [expr.unary.noexcept]: noexcept of member-pointer expressions (`&S::m`,
// `obj.*pm`, `(obj.*pmf)()`), as used by libc++'s `__is_nothrow_invocable_impl`.

struct S {
    int x;
    int f() noexcept { return x; }
    int g() { return x; }
};

S &get_s() noexcept;

static_assert(noexcept(&S::x));
static_assert(noexcept(get_s().*&S::x));
static_assert(noexcept((get_s().*&S::f)()));
static_assert(!noexcept((get_s().*&S::g)()));

int main()
{
    S s{42};
    int S::*pm = &S::x;
    int (S::*pf)() noexcept = &S::f;
    if (s.*pm != 42 || (s.*pf)() != 42)
        return 1;
    return 0;
}
