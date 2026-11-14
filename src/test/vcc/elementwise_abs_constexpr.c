/* vcc-flags: -std=c23 */
constexpr int i = __builtin_elementwise_abs(-2);
constexpr double d = __builtin_elementwise_abs(-2.5);
constexpr _Float16 h = __builtin_elementwise_abs((_Float16)-2.5);
constexpr signed char c = __builtin_elementwise_abs((signed char)-128);
_Static_assert(i == 2 && d == 2.5 && h == 2.5 && c == -128, "constexpr magnitude");
int main(void) { return i != 2 || d != 2.5 || h != 2.5 || c != -128; }
