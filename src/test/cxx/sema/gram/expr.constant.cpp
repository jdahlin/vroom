// Expressions S1b types and evaluates, in initializers and array bounds:
// bench/cxx-sema.py has clang check each expression's type and value
// category, and from C++11 on each constant's value.
const int one = 1;
const int two = one + one;
int by_bound[two * 3 + 1];
const long big = 1L << 40;
const unsigned wraps = 0u - 1;
const int shifted = -8 >> 1;
const int remainder_ = -7 % 3;
const int quotient = -7 / 2;
const bool truth = one < two && two != 3;
const char letter = 'a' + 1;
const unsigned char narrow_ = 300;
const short from_long = 70000L;
const int bits = ~0 ^ 0x0f | 0x100 & 0x1ff;
const int chosen = one ? 10 : 20;
const int comma_ = (one, two);
const int casted = (int)3.75 + int(2.5) + static_cast<int>(-1.5);
const unsigned long sizes = sizeof(int) + sizeof(long double) + sizeof one;
const unsigned long aligned = alignof(double);
const double third = 1.0 / 3;
const float half = 0.5f;
const double mixed = 3 + half * 2;
const int negated = !one + -two + +one;
const long long longest = 9223372036854775807LL;
const unsigned long long ullmax = 18446744073709551615ULL;
const int chars = sizeof("hello") + sizeof(u"hi");
const wchar_t wide = L'x';
const char16_t utf16 = u'y';
const int truncated = 3000000000u;
int sized[sizeof(long) * 2];
int from_char['z' - 'a' + 1];
int from_bool[true + true];
char text[] = "four";
char text_braced[] = {"five"};
wchar_t wide_text[] = L"wide";
char exact[5] = "four";
int listed[] = {1, 2, 3, 4};
int nested[2][3] = {{1, 2, 3}, {4, 5}};
int partial[10] = {1};
int scalar_braced{4};
int scalar_empty{};
int scalar_paren(5);
int *null_pointer = 0;
int *null_ptr2 = nullptr;
const int *const_pointer = &one;
const void *void_pointer = &one;
bool from_pointer = &one;
const int &bound_ref = one;
const long &converted_ref = one;
int &&rvalue_ref = 3;
const int &temporary_ref = 2.5;
int counter;
int &lvalue_ref = counter;
int increment = ++counter;
int assigned = counter = 3;
int compound = counter += 2;
int subscript = listed[2];
int *element = &listed[1];
int *advanced = listed + 2;
long difference = &listed[3] - &listed[0];

// [dcl.init]/8: value-initialization zero-initializes first; a base's
// user-provided constructor keeps the zeros of the members it leaves alone.
namespace value_init_zero_first {
struct A { constexpr A() {} int x; };
struct B : A { int y; };
static_assert(B().x == 0 && B().y == 0, "");
}
