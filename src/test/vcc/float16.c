/* Scalar half storage, rounding, conversions and arithmetic on Linux targets. */
#if defined(__aarch64__) || defined(__x86_64__)
typedef _Float16 half;
_Static_assert(sizeof(1.0f16) == 2, "half literal");
_Static_assert(0x1p-24F16 == 0x1p-24, "half subnormal literal");
static half rounded = (half)1.00048828125;
static half negative_zero = (half)-0.0;
__attribute__((noinline)) static half cvt(double x) { return (half)x; }
__attribute__((noinline)) static double widen(half x) { return (double)x; }
__attribute__((noinline)) static half calc(half a, half b) { return (a+b)*(a-b)/b; }
#endif
int main(void)
{
#if defined(__aarch64__) || defined(__x86_64__)
    union { half h; unsigned short u; } x, y;
    volatile half a = 3, b = 2;
    if (sizeof(half) != 2 || _Alignof(half) != 2 || rounded != 1) return 1;
    x.h = negative_zero;
    if (x.u != 0x8000 || (_Bool)x.h || !(-a < 0)) return 2;
    if (cvt(1.00048828125) != 1 || cvt(1.00146484375) != (half)1.001953125) return 3;
    if (calc(a,b) != (half)2.5 || (int)a != 3 || (unsigned)a != 3) return 4;
    if (a++ != 3 || a != 4 || --a != 3) return 5;
    for (unsigned i=0; i<65536; i++) {
        x.u = (unsigned short)i;
        volatile half z = x.h;
        unsigned mag = i & 0x7fff;
        if (!!__builtin_signbit(z) != !!(i & 0x8000)) return 7;
        if (!!__builtin_issignaling(z) != (mag > 0x7c00 && mag < 0x7e00)) return 12;
        if (!!__builtin_isnan(z) != (mag > 0x7c00)) return 8;
        if (!!__builtin_isinf(z) != (mag == 0x7c00)) return 9;
        if (!!__builtin_isfinite(z) != (mag < 0x7c00)) return 10;
        if (!!__builtin_isnormal(z) != (mag >= 0x400 && mag < 0x7c00)) return 11;
        if ((i & 0x7c00) == 0x7c00 && (i & 1023)) continue;
        x.u = (unsigned short)i;
        y.h = cvt(widen(x.h));
        if (y.u != x.u) return 6;
    }
#endif
    return 0;
}
