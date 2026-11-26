// glibc _FORTIFY_SOURCE=2 redeclares extern C functions as
// static inline __attribute__((__always_inline__, __overloadable__))
// with __pass_object_size__ (e.g. bits/wchar2.h:180 swprintf).

extern "C" {
extern int my_fortify_fn(const char *p, int n);

static inline __attribute__((__always_inline__, __overloadable__)) int
my_fortify_fn(const char *const __attribute__((__pass_object_size__(1))) p,
              int n)
{
    return p ? p[0] + n : n;
}
}

int main()
{
    const char s[] = "A";
    if (my_fortify_fn(s, 5) != 'A' + 5)
        return 1;
    return 0;
}
