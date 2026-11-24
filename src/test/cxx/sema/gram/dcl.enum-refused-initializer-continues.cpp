// [dcl.enum]/2: an enumerator-definition is analysed on its own: after one whose initializer analysis refuses
// (a class with a conversion function), a later enumerator with an initializer of its own is declared with its
// value; an implicit one that follows has none known.
struct S { constexpr operator int() const { return 5; } };
enum E : unsigned char { E5 = S(), E6, E10 = 3, E1 = E10 / 3, E2 };
static_assert(E10 == 3, "");
static_assert(E1 == 1, "");
