/* [ext:gnu_library_builtins] __builtin_isnan, isinf, isfinite, isnormal,
   signbit, fpclassify, isinf_sign, isunordered and the comparisons
   (GCC manual 6.56), of float, double and long double, at run time and in
   constant expressions; __builtin_huge_val, __builtin_inf, __builtin_nan,
   __builtin_nans and their f and l forms. */
constexpr double inf = __builtin_inf();
constexpr double nan = __builtin_nan("");
constexpr float finf = __builtin_inff();
constexpr float fnan = __builtin_nanf("");

static_assert(__builtin_isnan(nan));
static_assert(!__builtin_isnan(1.0));
static_assert(__builtin_isinf(inf));
static_assert(__builtin_isinf(-inf));
static_assert(!__builtin_isinf(1.0));
static_assert(__builtin_isfinite(1.0));
static_assert(!__builtin_isfinite(inf));
static_assert(__builtin_isnormal(1.0));
static_assert(!__builtin_isnormal(0.0));
static_assert(__builtin_signbit(-0.0));
static_assert(!__builtin_signbit(2.0));
static_assert(__builtin_isinf_sign(inf) == 1);
static_assert(__builtin_isinf_sign(-inf) == -1);
static_assert(__builtin_isinf_sign(0.0) == 0);
static_assert(__builtin_isnan(fnan) && __builtin_isinf(finf));
static_assert(__builtin_fpclassify(0, 1, 2, 3, 4, 0.0) == 4);
static_assert(__builtin_fpclassify(0, 1, 2, 3, 4, 1.5) == 2);
static_assert(__builtin_fpclassify(0, 1, 2, 3, 4, inf) == 1);
static_assert(__builtin_fpclassify(0, 1, 2, 3, 4, nan) == 0);
static_assert(__builtin_isunordered(nan, 1.0));
static_assert(!__builtin_isunordered(1.0, 2.0));
static_assert(__builtin_isless(1.0, 2.0));
static_assert(!__builtin_isless(nan, 2.0));
static_assert(__builtin_isgreater(3.0, 2.0));
static_assert(__builtin_islessequal(2.0, 2.0));
static_assert(__builtin_isgreaterequal(2.0, 2.0));
static_assert(__builtin_islessgreater(1.0, 2.0));
static_assert(!__builtin_islessgreater(nan, 2.0));
static_assert(__builtin_huge_val() == inf);
static_assert(__builtin_huge_valf() == finf);
static_assert(__builtin_isinf(__builtin_infl()));
static_assert(__builtin_isnan(__builtin_nans("")));
static_assert(__builtin_isnan(__builtin_nanl("")));
static_assert(__builtin_isnan(__builtin_nansf("")));

template <class T> static int classify(T v)
{
    return __builtin_isnan(v) * 1 + __builtin_isinf(v) * 2 +
           __builtin_isfinite(v) * 4 + __builtin_isnormal(v) * 8 +
           __builtin_signbit(v) * 16;
}

int main()
{
    volatile double zero = 0.0;
    double n = zero / zero, i = 1.0 / zero;
    if (classify(n) != 1 + 16 && classify(n) != 1)
        return 1;
    if (classify(i) != 2)
        return 2;
    if (classify(-i) != 2 + 16)
        return 3;
    if (classify(1.0) != 4 + 8)
        return 4;
    if (classify(-0.0) != 4 + 16)
        return 5;
    if (classify(1.0f) != 4 + 8)
        return 6;
    if (classify(__builtin_inff()) != 2)
        return 7;
    if (!__builtin_isunordered(n, 1.0) || __builtin_isless(n, 1.0))
        return 8;
    if (__builtin_isinf_sign(-i) != -1)
        return 9;
    return 0;
}
