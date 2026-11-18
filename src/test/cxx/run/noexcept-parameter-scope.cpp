// [basic.scope.param]/1: a function's parameters in its noexcept-specifier;
// [temp.dep.constexpr]: a dependent one decided per specialization, with a
// pack expansion of the function parameter pack
int add(int a, int b) noexcept { return a + b; }
long sub(long a, long b) { return a - b; }
int twice(int a) noexcept(noexcept(add(a, a))) { return add(a, a); }
template <class T> T one(T a) noexcept(noexcept(add(a, a))) { return add(a, a); }
template <class T> T other(T a) noexcept(noexcept(sub(a, a))) { return sub(a, a); }
template <class... A> int call(A... a) noexcept(noexcept(add(a...))) { return add(a...); }
template <class... A> long call_sub(A... a) noexcept(noexcept(sub(a...))) { return sub(a...); }
static_assert(noexcept(twice(1)), "");
static_assert(noexcept(one(1)), "");
static_assert(!noexcept(other(1L)), "");
static_assert(noexcept(call(1, 2)), "");
static_assert(!noexcept(call_sub(5L, 2L)), "");
int main()
{
    return twice(2) == 4 && one(1) == 2 && other(3L) == 0 && call(1, 2) == 3 &&
                   call_sub(5L, 2L) == 3
               ? 0
               : 1;
}
