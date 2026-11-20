/* [ext:clang_builtin_functions] __builtin_invoke(callable, args...)
   (clang 22, Builtins.td): [func.require]'s INVOKE for a function pointer,
   a member function pointer, a pointer to a data member and a function
   object; libc++'s std::invoke is written with it. */
struct S {
    int v;
    int get() const { return v; }
    int add(int d) { return v + d; }
};
static int twice(int x) { return 2 * x; }
struct Fn {
    int operator()(int a, int b) const { return a - b; }
};

int main()
{
    S s = {10};
    if (__builtin_invoke(twice, 4) != 8)
        return 1;
    if (__builtin_invoke(&S::get, s) != 10)
        return 2;
    if (__builtin_invoke(&S::add, &s, 5) != 15)
        return 3;
    if (__builtin_invoke(&S::v, s) != 10)
        return 4;
    if (__builtin_invoke(Fn(), 9, 4) != 5)
        return 5;
    auto lam = [](int a) { return a + 1; };
    if (__builtin_invoke(lam, 1) != 2)
        return 6;
    return 0;
}
