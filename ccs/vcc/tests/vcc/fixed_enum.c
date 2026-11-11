/* vcc-flags: -std=c23 */
#include <limits.h>
enum Small : unsigned char;
_Static_assert(sizeof(enum Small) == 1, "forward declaration is complete");
enum Small : unsigned char { SMALL_ZERO, SMALL_MAX = UCHAR_MAX };
enum Small : unsigned char;
enum Unsigned : unsigned int { UMAX = UINT_MAX, UWRAP = UMAX + 1 };
enum Huge : unsigned long long { HMAX = ULLONG_MAX, HALMOST = HMAX - 1 };
enum Signed : signed char { NEG = SCHAR_MIN, NEXT, POS = SCHAR_MAX };
enum Boolean : _Bool { NO, YES };
_Static_assert(sizeof(enum Small) == 1 && _Alignof(enum Small) == 1, "small layout");
_Static_assert(__builtin_types_compatible_p(__typeof__(SMALL_ZERO), enum Small), "enumerator type");
_Static_assert(__builtin_types_compatible_p(enum Huge, unsigned long long), "underlying type");
_Static_assert(UWRAP == 0, "unsigned arithmetic wraps inside enum");
_Static_assert(HMAX == ULLONG_MAX && HALMOST == ULLONG_MAX - 1, "64-bit values");
_Static_assert(NEXT == SCHAR_MIN + 1, "signed implicit increment");
struct Container { char c; enum Huge huge; enum Small small; };
static enum Small pass(enum Small x) { return x; }
static enum Huge pass_huge(enum Huge x) { return x; }
int main(void) {
    struct Container c = { 3, HMAX, SMALL_MAX };
    enum Signed s = NEG;
    enum Boolean b = YES;
    if (pass(c.small) != UCHAR_MAX || pass_huge(c.huge) != ULLONG_MAX) return 1;
    if (s != SCHAR_MIN || b != 1 || NO != 0) return 2;
    return 0;
}
