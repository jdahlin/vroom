/* diag-flags: -std=c23 -Wno-conversion -Wno-constant-conversion */
/* diag-differs: vcc points at the declared object for definition constraints and at the whole string for character conversion, rather than clang's initializer/end-of-string location. Definition errors are preserved, and malformed initializer recovery must not leak constexpr conversion rules into the next ordinary declaration. */
/* Constants must be representable before implicit conversion; an explicit cast
   deliberately performs a conversion and is permitted. */
const int ordinary = 3;
constexpr int not_constant = ordinary;
constexpr unsigned negative = -1;
constexpr float rounded = 0.1;
constexpr int wrong_category = 0.5;
constexpr volatile int volatile_object = 0;
constexpr int uninitialized;
constexpr int *non_null = (int *)4;
struct Record { unsigned char byte; float number; };
constexpr struct Record record = { 300, 0.1 };
constexpr unsigned char bytes[] = { -1, 256 };
constexpr signed char text[] = u8"\xFF";
constexpr int returns_value(void) { return 0; }
typedef constexpr int not_an_object;
void parameter(constexpr int value);
constexpr unsigned accepted_cast = (unsigned)-1;
constexpr float accepted_rounding = (float)0.1;

void recovery(void) {
    constexpr int malformed = ;
    int ordinary_truncation = 0.5;
    (void)ordinary_truncation;
}
