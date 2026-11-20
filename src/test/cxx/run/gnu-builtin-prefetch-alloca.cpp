/* [ext:gnu_library_builtins] void __builtin_prefetch(const void *, ...),
   void *__builtin_alloca(size_t), __builtin_alloca_with_align,
   __builtin_return_address(unsigned), __builtin_frame_address(unsigned)
   (GCC manual 6.63, 6.56): pointer-typed parameters that take the standard
   conversions of [expr.call]. */
typedef unsigned long size_t;

static int sum(int n)
{
    int *a = (int *)__builtin_alloca(n * sizeof(int));
    for (int i = 0; i < n; i++)
        a[i] = i;
    int s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}

static int touch(const int *p)
{
    __builtin_prefetch(p);
    __builtin_prefetch(p, 1);
    __builtin_prefetch(p, 0, 3);
    return *p;
}

__attribute__((noinline)) static void *frame()
{
    return __builtin_frame_address(0);
}

__attribute__((noinline)) static void *caller()
{
    return __builtin_return_address(0);
}

int main()
{
    if (sum(10) != 45)
        return 1;
    int v = 7;
    if (touch(&v) != 7)
        return 2;
    char local;
    void *f = frame();
    if (!f || (char *)f == &local + 100000)
        return 3;
    if (!caller())
        return 4;
    return 0;
}
