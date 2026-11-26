/* GNU vector operations on 16-byte vectors, every operator on every lane
   type, with pseudo-random and extreme lanes: x86-64's SSE2 selection
   (arch/x64/codegen/simd.cpp) against the lane-wise meaning */
#include <stdio.h>
#include <string.h>

static unsigned long long hash = 0xcbf29ce484222325ull;
static unsigned long long state = 1;

static void add(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    for (int i = 0; i < 16; i++)
        hash = (hash ^ b[i]) * 0x100000001b3ull;
}

static void fill(void *p, int round)
{
    unsigned long long r[2];
    for (int k = 0; k < 2; k++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        r[k] = state ^ (state >> 29);
        if (round & 1)
            r[k] &= 0x8100008100810081ull;
        if (round % 5 == 0)
            r[k] |= 0x7f00ff007fff80ffull;
    }
    memcpy(p, r, 16);
}

#define OP(V, R, name, expr)                                                   \
    static __attribute__((noinline)) void name(const V *pa, const V *pb,       \
                                               int n)                          \
    {                                                                          \
        V a = *pa, b = *pb;                                                    \
        R r = (R)(expr);                                                       \
        (void)n;                                                               \
        add(&r);                                                               \
    }

#define COMMON(T, V, M)                                                        \
    typedef T V __attribute__((vector_size(16)));                              \
    OP(V, V, V##_add, a + b)                                                   \
    OP(V, V, V##_sub, a - b)                                                   \
    OP(V, M, V##_eq, a == b)                                                   \
    OP(V, M, V##_ne, a != b)                                                   \
    OP(V, M, V##_lt, a < b)                                                    \
    OP(V, M, V##_gt, a > b)                                                    \
    OP(V, M, V##_le, a <= b)                                                   \
    OP(V, M, V##_ge, a >= b)

#define INTEGER(T, U, V, M, BITS)                                              \
    COMMON(T, V, M)                                                            \
    typedef U V##_u __attribute__((vector_size(16)));                          \
    OP(V, V, V##_mul, (V)((V##_u)a * (V##_u)b))                                \
    OP(V, V, V##_and, a & b)                                                   \
    OP(V, V, V##_or, a | b)                                                    \
    OP(V, V, V##_xor, a ^ b)                                                   \
    OP(V, V, V##_not, ~a)                                                      \
    OP(V, V, V##_neg, (V)-(V##_u)a)                                            \
    OP(V, V, V##_shl, (V)((V##_u)a << (n & (BITS - 1))))                       \
    OP(V, V, V##_shr, a >> (n & (BITS - 1)))                                   \
    OP(V, V, V##_plus_scalar, (V)((V##_u)a + (U)n))                            \
    OP(V, V, V##_scalar_xor, (T)n ^ a)                                         \
    static void V##_run(const void *x, const void *y, int n)                   \
    {                                                                          \
        const V *a = (const V *)x, *b = (const V *)y;                          \
        V##_add(a, b, n), V##_sub(a, b, n), V##_mul(a, b, n);                  \
        V##_eq(a, b, n), V##_ne(a, b, n), V##_lt(a, b, n);                     \
        V##_gt(a, b, n), V##_le(a, b, n), V##_ge(a, b, n);                     \
        V##_and(a, b, n), V##_or(a, b, n), V##_xor(a, b, n);                   \
        V##_not(a, b, n), V##_neg(a, b, n), V##_shl(a, b, n);                  \
        V##_shr(a, b, n), V##_plus_scalar(a, b, n), V##_scalar_xor(a, b, n);   \
        V##_eq(a, a, n), V##_le(a, a, n);                                      \
    }

#define FLOATING(T, V, M)                                                      \
    COMMON(T, V, M)                                                            \
    OP(V, V, V##_mul, a *b)                                                    \
    OP(V, V, V##_div, a / b)                                                   \
    static void V##_run(int n)                                                 \
    {                                                                          \
        V a, b;                                                                \
        for (unsigned i = 0; i < 16 / sizeof(T); i++) {                        \
            a[i] = (T)(((n + (int)i) % 7) - 3) / 2;                            \
            b[i] = (T)(((n * 3 + (int)i) % 5) - 2);                            \
            if (b[i] == 0)                                                     \
                b[i] = a[i] + 8;                                               \
        }                                                                      \
        V##_add(&a, &b, n), V##_sub(&a, &b, n), V##_mul(&a, &b, n);            \
        V##_div(&a, &b, n), V##_eq(&a, &b, n), V##_ne(&a, &b, n);              \
        V##_lt(&a, &b, n), V##_gt(&a, &b, n), V##_le(&a, &b, n);               \
        V##_ge(&a, &b, n);                                                     \
    }

typedef signed char s8x __attribute__((vector_size(16)));
typedef short s16x __attribute__((vector_size(16)));
typedef int s32x __attribute__((vector_size(16)));
typedef long long s64x __attribute__((vector_size(16)));

INTEGER(signed char, unsigned char, i8, s8x, 8)
INTEGER(unsigned char, unsigned char, u8, s8x, 8)
INTEGER(short, unsigned short, i16, s16x, 16)
INTEGER(unsigned short, unsigned short, u16, s16x, 16)
INTEGER(int, unsigned, i32, s32x, 32)
INTEGER(unsigned, unsigned, u32, s32x, 32)
INTEGER(long long, unsigned long long, i64, s64x, 64)
INTEGER(unsigned long long, unsigned long long, u64, s64x, 64)
FLOATING(float, f32, s32x)
FLOATING(double, f64, s64x)

int main(void)
{
    for (int n = 0; n < 600; n++) {
        unsigned long long a[2] __attribute__((aligned(16)));
        unsigned long long b[2] __attribute__((aligned(16)));
        fill(a, n), fill(b, n + 2);
        i8_run(a, b, n), u8_run(a, b, n), i16_run(a, b, n);
        u16_run(a, b, n), i32_run(a, b, n), u32_run(a, b, n);
        i64_run(a, b, n), u64_run(a, b, n);
        f32_run(n), f64_run(n);
    }
    printf("%016llx\n", hash);
    return 0;
}
