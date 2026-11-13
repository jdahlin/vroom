/* The freestanding runtime must supply the same division ABI as libgcc. */
extern _Complex float __divsc3(float, float, float, float);
extern _Complex double __divdc3(double, double, double, double);
#if __LDBL_MANT_DIG__ == 113
extern _Complex long double __divtc3(long double, long double, long double, long double);
#define DIVL __divtc3
#elif __LDBL_MANT_DIG__ == 64
extern _Complex long double __divxc3(long double, long double, long double, long double);
#define DIVL __divxc3
#endif
#define CHECK(T, FN, BIG, SMALL, INF) do { \
    _Complex T z = FN(BIG, BIG, BIG, BIG); \
    if (__real__ z != 1 || __imag__ z != 0) return 1; \
    z = FN(SMALL, SMALL, SMALL, SMALL); \
    if (__real__ z != 1 || __imag__ z != 0) return 2; \
    z = FN(3, 4, INF, INF); \
    if (__real__ z != 0 || __imag__ z != 0) return 3; \
    z = FN(INF, INF, 1, 0); \
    if (!__builtin_isinf(__real__ z) || !__builtin_isinf(__imag__ z)) return 4; \
    z = FN(1, -1, -(T)0.0, (T)0.0); \
    if (!__builtin_isinf(__real__ z) || !__builtin_signbit(__real__ z) || \
        !__builtin_isinf(__imag__ z) || __builtin_signbit(__imag__ z)) return 5; \
} while (0)
int main(void)
{
    CHECK(float, __divsc3, 0x1p100f, 0x1p-140f, __builtin_inff());
    CHECK(double, __divdc3, 0x1p1000, 0x1p-1070, __builtin_inf());
#ifdef DIVL
    CHECK(long double, DIVL, 0x1p16000L, 0x1p-16440L, __builtin_infl());
#endif
    return 0;
}
