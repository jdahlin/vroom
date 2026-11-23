/* [ext:gnu_library_builtins] signaling NaN constructors used by <limits>. */
constexpr float signaling_nan() { return __builtin_nansf(""); }

constexpr float default_nan = signaling_nan();
constexpr float payload_nan = __builtin_nansf("0x123");
constexpr double double_nan = __builtin_nans("0x123");

static_assert(__builtin_bit_cast(unsigned, default_nan) == 0x7fa00000u);
static_assert(__builtin_bit_cast(unsigned, payload_nan) == 0x7f800123u);
static_assert(__builtin_bit_cast(unsigned long long, double_nan) ==
              0x7ff0000000000123ull);

static_assert(__builtin_issignaling(default_nan));
static_assert(!__builtin_issignaling(__builtin_nanf("")));
static_assert(__builtin_issignaling(__builtin_bit_cast(float, 0x7f800123u)));
static_assert(__builtin_isinf_sign(-__builtin_inff()) == -1);
static_assert(__builtin_isinf_sign(default_nan) == 0);

constexpr float negative_infinity = -__builtin_inff();
volatile float stored_negative_infinity = negative_infinity;
volatile float stored_float_nan = payload_nan;
volatile double stored_double_nan = double_nan;

int main() {
    float f = stored_float_nan;
    double d = stored_double_nan;
    volatile long double long_nan = __builtin_nansl("0x123");
    return !__builtin_issignaling(f) || !__builtin_issignaling(d) ||
           !__builtin_issignaling(long_nan) ||
           __builtin_isinf_sign(stored_negative_infinity) != -1 ||
           __builtin_fpclassify(0, 1, 2, 3, 4, f) != 0 ||
           __builtin_bit_cast(unsigned, f) != 0x7f800123u ||
           __builtin_bit_cast(unsigned long long, d) != 0x7ff0000000000123ull;
}
