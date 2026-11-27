// [ext:clang_pragma_attribute] and [ext:gnu_attributes] target: the features
// are those of the functions between push and pop, or of the one declared.
// On x86-64 their code uses SSSE3 and AVX2; elsewhere the pragma and the
// templates' definitions are all there is to check.
#if defined(__x86_64__)
#define TARGET(f) __attribute__((target(f)))
typedef char v16 __attribute__((vector_size(16)));
#else
#define TARGET(f)
#endif

#if defined(__x86_64__)
#pragma clang attribute push(__attribute__((target("sse2,ssse3"))), \
                             apply_to = function)
#endif

static int reversed(int i)
{
#if defined(__x86_64__)
    v16 a = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    v16 m = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    v16 r = __builtin_ia32_pshufb128(a, m);
    return r[i];
#else
    return 15 - i;
#endif
}

struct shuffler {
    template <int N> static int at() { return reversed(N); }
};

#if defined(__x86_64__)
#pragma clang attribute pop
#endif

TARGET("avx2") static int doubled(int i)
{
#if defined(__x86_64__)
    typedef int v8i __attribute__((vector_size(32)));
    volatile int x = i;
    v8i a = {x, x, x, x, x, x, x, x};
    v8i s = a + a;
    return s[7];
#else
    return i * 2;
#endif
}

static bool host_has()
{
#if defined(__x86_64__)
    unsigned a, b, c, d;
    asm("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    return b & (1u << 5);
#else
    return true;
#endif
}

int main()
{
    if (!host_has())
        return 0;
    if (shuffler::at<0>() != 15 || shuffler::at<5>() != 10)
        return 1;
    if (doubled(21) != 42)
        return 2;
    return 0;
}
