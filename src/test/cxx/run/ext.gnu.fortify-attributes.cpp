// Clang/glibc _FORTIFY_SOURCE=2 attributes: __overloadable__,
// __pass_object_size__, __pass_dynamic_object_size__.

extern "C" {
static inline __attribute__((__always_inline__, __overloadable__)) int
fortify_fn(const char *const __attribute__((__pass_object_size__(1))) p)
{
    return p ? p[0] : 0;
}

static inline __attribute__((__always_inline__, __overloadable__)) int
fortify_dyn(const char *const __attribute__((__pass_dynamic_object_size__(0))) p)
{
    return p ? p[0] : 0;
}
}

int main()
{
    const char s[] = "Z";
    if (fortify_fn(s) != 'Z')
        return 1;
    if (fortify_dyn(s) != 'Z')
        return 2;
    return 0;
}
