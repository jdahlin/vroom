/* [ext:gnu_library_builtins] void *__builtin_assume_aligned(const void *,
   size_t, ...) (GCC manual 6.63; clang's Builtins.td): the pointer
   returned is the first argument; libc++'s std::assume_aligned wraps it.
   The alignment is a power of two, the optional third argument an offset. */
typedef unsigned long size_t;

static int buffer[8] __attribute__((aligned(16))) = {1, 2, 3, 4, 5, 6, 7, 8};

template <size_t N, class T> static T *assume(T *p)
{
    return static_cast<T *>(__builtin_assume_aligned(p, N));
}

static int *offset_form(int *p)
{
    /* p + 4 bytes is 8-aligned: p itself is 8-aligned, offset 4 */
    return (int *)__builtin_assume_aligned(p, 16, 0);
}

static const void *constant_form(const void *p)
{
    return __builtin_assume_aligned(p, 1);
}

constexpr int constant_evaluated(int *p)
{
    (void)__builtin_assume_aligned(p, 4);
    return *p;
}

int main()
{
    int *p = assume<16>(buffer);
    if (p != buffer || p[3] != 4)
        return 1;
    if (offset_form(buffer + 4) != buffer + 4)
        return 2;
    if (constant_form(buffer) != buffer)
        return 3;
    if (constant_evaluated(buffer + 2) != 3)
        return 4;
    return 0;
}
