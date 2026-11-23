/* [ext:gnu_va_arg] [ext:gnu_library_builtins] __builtin_va_start,
   __builtin_va_arg, __builtin_va_copy and __builtin_va_end in C++ (the
   builtins <stdarg.h>'s macros expand to). */
static int sum(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++)
        s += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return s;
}

/* a va_list passed on, as vprintf's callers do */
static long long next_long(__builtin_va_list &ap)
{
    return __builtin_va_arg(ap, long long);
}

static long long wide(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    long long s = 0;
    while (n-- > 0)
        s += next_long(ap);
    __builtin_va_end(ap);
    return s;
}

/* a copy reads the same arguments again */
static int twice(int n, ...)
{
    __builtin_va_list ap, bp;
    __builtin_va_start(ap, n);
    __builtin_va_copy(bp, ap);
    int a = 0, b = 0;
    for (int i = 0; i < n; i++)
        a += __builtin_va_arg(ap, int);
    for (int i = 0; i < n; i++)
        b += __builtin_va_arg(bp, int);
    __builtin_va_end(ap);
    __builtin_va_end(bp);
    return a * 1000 + b;
}

static double mixed(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    double d = __builtin_va_arg(ap, double);
    int *p = __builtin_va_arg(ap, int *);
    unsigned long u = __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return d + *p + u;
}

/* a va_list parameter (a copy of the caller's), itself copied */
static int through(__builtin_va_list ap)
{
    __builtin_va_list again;
    __builtin_va_copy(again, ap);
    int a = __builtin_va_arg(ap, int);
    int b = __builtin_va_arg(again, int);
    __builtin_va_end(again);
    return a * 100 + b;
}

static int forward(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int r = through(ap);
    __builtin_va_end(ap);
    return r;
}

template <class T> static T first(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    T t = __builtin_va_arg(ap, T);
    __builtin_va_end(ap);
    return t;
}

int main()
{
    if (sum(4, 1, 2, 3, 4) != 10)
        return 1;
    if (wide(3, 1LL << 40, 2LL, 3LL) != (1LL << 40) + 5)
        return 2;
    if (twice(3, 5, 6, 7) != 18018)
        return 3;
    int k = 4;
    if (mixed(0, 0.5, &k, 10ul) != 14.5)
        return 4;
    if (first<int>(1, 77) != 77)
        return 5;
    if (forward(0, 7, 8) != 707)
        return 6;
    return 0;
}
