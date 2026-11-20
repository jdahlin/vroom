/* [ext:clang_builtin_functions] __builtin_verbose_trap(category, message)
   (the two arguments are string literals) and
   __builtin_assume_dereferenceable(pointer, size) (clang 21): libc++ calls
   the first in its assertions and the second in __assume_valid_range. */
typedef unsigned long size_t;

[[noreturn]] static void fail()
{
    __builtin_verbose_trap("category", "message");
}

static int dereferenceable(int *p)
{
    __builtin_assume_dereferenceable(p, sizeof(int));
    return *p;
}

static int counted(int *p, size_t n)
{
    __builtin_assume_dereferenceable(p, n * sizeof(int));
    int sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += p[i];
    return sum;
}

int main(int argc, char **)
{
    if (argc > 5)
        fail();
    int x = 4;
    if (dereferenceable(&x) != 4)
        return 1;
    int a[3] = {1, 2, 3};
    if (counted(a, 3) != 6)
        return 2;
    return 0;
}
