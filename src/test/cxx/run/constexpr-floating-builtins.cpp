/* [ext:gnu_library_builtins] floating constants used by libc++ <limits>. */
constexpr float infinity() { return __builtin_huge_valf(); }
constexpr float quiet_nan() { return __builtin_nanf(""); }

constexpr float positive_infinity = infinity();
constexpr float negative_infinity = -positive_infinity;
constexpr float quiet_nan_value = quiet_nan();
constexpr float octal_nan = __builtin_nanf("010");
constexpr double payload_nan = __builtin_nan("0x42");

static_assert(__builtin_bit_cast(unsigned, positive_infinity) == 0x7f800000u);
static_assert(__builtin_bit_cast(unsigned, negative_infinity) == 0xff800000u);
static_assert(__builtin_bit_cast(unsigned, quiet_nan_value) == 0x7fc00000u);
static_assert(__builtin_bit_cast(unsigned long long, payload_nan) ==
              0x7ff8000000000042ull);
static_assert(__builtin_bit_cast(unsigned, octal_nan) ==
              0x7fc00008u);
static_assert(__builtin_bit_cast(unsigned long long, __builtin_inf()) ==
              0x7ff0000000000000ull);

volatile float stored_infinity = positive_infinity;
volatile float stored_float_nan = octal_nan;
volatile double stored_double_nan = payload_nan;

int main() {
    float inf = stored_infinity;
    float float_nan = stored_float_nan;
    double double_nan = stored_double_nan;
    return __builtin_bit_cast(unsigned, inf) != 0x7f800000u ||
           __builtin_bit_cast(unsigned, float_nan) != 0x7fc00008u ||
           __builtin_bit_cast(unsigned long long, double_nan) !=
               0x7ff8000000000042ull;
}
