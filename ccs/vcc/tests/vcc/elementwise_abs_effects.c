/* vcc-flags: -O1 */
static int count;
static volatile int vi = -9;
static volatile float vf = -2.0f;
static volatile double vd = -3.0;
static volatile _Float16 vh = -4.0;
static _Atomic int atomic_value = -11;
static const volatile signed char qualified_char = -7;
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_abs(atomic_value)), int), "atomic operand yields plain result");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_abs(qualified_char)), signed char), "drop qualifiers without integer promotion");
/* Keep these out of line so instruction inspection can verify the volatile
 * load even though its value does not affect the known result. */
__attribute__((noinline)) int read_atomic(void) { return __builtin_elementwise_abs((atomic_value, -11)); }
__attribute__((noinline)) int read_int(void) { return __builtin_elementwise_abs((vi, -2)); }
__attribute__((noinline)) float read_float(void) { return __builtin_elementwise_abs((vf, -2.0f)); }
__attribute__((noinline)) double read_double(void) { return __builtin_elementwise_abs((vd, -3.0)); }
__attribute__((noinline)) _Float16 read_half(void) { return __builtin_elementwise_abs((vh, (_Float16)-4.0)); }
int main(void)
{
    int i = __builtin_elementwise_abs((++count, -2));
    if (i != 2 || count != 1) return 1;
    i = __builtin_elementwise_abs((++count, 3));
    if (i != 3 || count != 2) return 2;
    float f = __builtin_elementwise_abs((++count, -2.0f));
    if (f != 2 || count != 3) return 3;
    double d = __builtin_elementwise_abs((++count, -3.0));
    if (d != 3 || count != 4) return 4;
    _Float16 h = __builtin_elementwise_abs((++count, (_Float16)-4.0));
    if (h != 4 || count != 5) return 5;
    long double l = __builtin_elementwise_abs((++count, -5.0L));
    if (l != 5 || count != 6) return 6;
    signed char c = __builtin_elementwise_abs((++count, (signed char)-128));
    if (c != -128 || count != 7) return 7;
    i = __builtin_elementwise_abs((++count, (vi++, -6)));
    if (i != 6 || count != 8 || vi != -8) return 8;
    if (__builtin_elementwise_abs(vi++) != 8 || vi != -7) return 9;
    if (__builtin_elementwise_abs(vf++) != 2 || vf != -1) return 10;
    if (__builtin_elementwise_abs(vh++) != 4 || vh != -3) return 11;
    if (__builtin_elementwise_abs(atomic_value) != 11 || __builtin_elementwise_abs(qualified_char) != 7) return 12;
    if (read_atomic() != 11 || read_int() != 2 || read_float() != 2 || read_double() != 3 || read_half() != 4) return 13;
    return 0;
}
