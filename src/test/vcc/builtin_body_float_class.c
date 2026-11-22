/* builtins with a C body (builtin-body.cpp): __builtin_iszero and
   __builtin_issubnormal on every floating type, constants, and one evaluation
   of the argument.  Also clean under -Wfloat-equal. */
/* vcc-flags: -Wall -Wextra -Wfloat-equal -Werror */
_Static_assert(__builtin_iszero(0.0), "");
_Static_assert(__builtin_iszero(-0.0f), "");
_Static_assert(!__builtin_iszero(1.0L), "");
_Static_assert(!__builtin_iszero(__builtin_nan("")), "");
_Static_assert(__builtin_issubnormal(__DBL_DENORM_MIN__), "");
_Static_assert(__builtin_issubnormal(-__FLT_DENORM_MIN__), "");
_Static_assert(!__builtin_issubnormal(1.0f), "");
_Static_assert(!__builtin_issubnormal(0.0), "");
_Static_assert(!__builtin_issubnormal(__builtin_inf()), "");
_Static_assert(!__builtin_issubnormal(__builtin_nan("")), "");
static int bound[__builtin_iszero(0.0) + 1];
static int constant_zero = __builtin_iszero(-0.0);

static int calls;
static float next(float v)
{
    calls++;
    return v;
}

#define CHECK_TYPE(T, suffix, tiny)                                           \
    static int check_##suffix(void)                                           \
    {                                                                         \
        volatile T v[] = {__builtin_nan##suffix(""), -__builtin_inf##suffix(),\
                          -1.0, -tiny, -0.0, 0.0, tiny, 1.0,                  \
                          __builtin_inf##suffix()};                           \
        for (int i = 0; i < 9; i++) {                                         \
            T x = v[i];                                                       \
            if (!!__builtin_iszero(x) != (i == 4 || i == 5))                  \
                return 1;                                                     \
            if (!!__builtin_issubnormal(x) != (i == 3 || i == 6))             \
                return 2;                                                     \
        }                                                                     \
        return 0;                                                             \
    }
CHECK_TYPE(float, f, __FLT_DENORM_MIN__)
CHECK_TYPE(double, , __DBL_DENORM_MIN__)
CHECK_TYPE(long double, l, __LDBL_DENORM_MIN__)

int main(void)
{
    volatile int fail = check_f() | check_() | check_l() | !constant_zero;
    if (fail || bound[0])
        return 1;
    if (!__builtin_iszero(next(0.0f)) || calls != 1)
        return 2;
    if (__builtin_issubnormal(next(1.0f)) || calls != 2)
        return 3;
    if (!__builtin_issubnormal(next(__FLT_DENORM_MIN__)) || calls != 3)
        return 4;
    {
        float a[2] = {0.0f, 1.0f};
        float *p = a;
        if (!__builtin_iszero(*p++) || p != a + 1 || __builtin_iszero(*p++) || p != a + 2)
            return 5;
    }
    return 0;
}
