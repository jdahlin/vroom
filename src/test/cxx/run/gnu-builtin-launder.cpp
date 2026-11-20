/* [ext:clang_builtin_functions] T *__builtin_launder(T *) (clang's
   Builtins.td, custom type checking): [ptr.launder]'s std::launder, the
   pointer to the object that is in the storage the argument points to; the
   argument is a pointer to an object type, not to a function or void. */
struct S {
    const int a;
    int b;
};

static S *relaunder(S *p)
{
    return __builtin_launder(p);
}

constexpr int *through(int *p)
{
    return __builtin_launder(p);
}

template <class T> T *wrap(T *p)
{
    return __builtin_launder(p);
}

int main()
{
    S s = {1, 2};
    if (relaunder(&s) != &s || relaunder(&s)->a != 1)
        return 1;
    int x = 5;
    if (through(&x) != &x || wrap(&x) != &x)
        return 2;
    const volatile char c = 'a';
    const volatile char *q = wrap(&c);
    if (q != &c)
        return 3;
    constexpr int arr[2] = {1, 2};
    static_assert(*__builtin_launder(&arr[1]) == 2);
    return 0;
}
