typedef int v4i __attribute__((vector_size(16)));
typedef short v8s __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));
static int calls;
static v4i ints(void) { calls++; return (v4i){(-2147483647-1),-7,0,2147483647}; }
static _Float16 half_abs(_Float16 x) { return __builtin_elementwise_abs(x); }
static signed char scalar(void) { calls++; return -127; }
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_abs((signed char)1)), signed char), "no integer promotion");
_Static_assert(__builtin_elementwise_abs(-2147483647-1) == (-2147483647-1), "signed minimum wraps");
_Static_assert(__builtin_elementwise_abs((_Float16)-1.5) == (_Float16)1.5, "half constant expression");
_Static_assert(__builtin_elementwise_abs(-7) == 7, "integer constant expression");
_Static_assert(__builtin_elementwise_abs(-0x1.000002p0f) == 0x1.000002p0f, "float precision");
_Static_assert(__builtin_elementwise_abs(-0x1.0000000000001p0) == 0x1.0000000000001p0, "double precision");
_Static_assert(__builtin_elementwise_abs(-0x1.000000000000001p0L) == 0x1.000000000000001p0L, "long double precision");
int main(void)
{
    v4i a = __builtin_elementwise_abs(ints());
    if (calls != 1 || a[0] != (-2147483647-1) || a[1] != 7 || a[2] != 0 || a[3] != 2147483647) return 1;
    if (__builtin_elementwise_abs(scalar()) != 127 || calls != 2) return 2;
    v8s small = {-32768,-1,-1234,32767,0,1,2,-2};
    small = __builtin_elementwise_abs(small);
    if (small[0] != -32768 || small[1] != 1 || small[2] != 1234 || small[7] != 2) return 3;
    v4f f = {-0.0f,-3.25f,-__builtin_inff(),-__builtin_nanf("")};
    f = __builtin_elementwise_abs(f);
    if (__builtin_signbit(f[0]) || f[1] != 3.25f || !__builtin_isinf(f[2]) || __builtin_signbit(f[2]) || !__builtin_isnan(f[3]) || __builtin_signbit(f[3])) return 4;
    volatile long long l = (-9223372036854775807LL-1);
    if (__builtin_elementwise_abs(l) != l) return 5;
    volatile double d = -0.0;
    if (__builtin_signbit(__builtin_elementwise_abs(d))) return 6;
    union { _Float16 f; unsigned short u; } h = {.u=0xfc01}, r;
    r.f = half_abs(h.f);
    if (r.u != 0x7c01) return 7;
    volatile long double ld = -0.0L;
    if (__builtin_signbit(__builtin_elementwise_abs(ld))) return 8;
    union { double f; unsigned long long u; } dn = {.u=0xfff0000000000001ULL}, dr;
    volatile double signaling = dn.f;
    dr.f = __builtin_elementwise_abs(signaling);
    if (dr.u != 0x7ff0000000000001ULL) return 9;
    volatile double precise = -0x1.0000000000001p0;
    if (__builtin_elementwise_abs(precise) != 0x1.0000000000001p0) return 10;
    return 0;
}
