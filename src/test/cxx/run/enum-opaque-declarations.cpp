/* [dcl.enum]/3: an opaque-enum-declaration declares an enumeration with a
   fixed underlying type, complete from its enum-base ([dcl.enum]/6), and a
   later enum-specifier or opaque-enum-declaration redeclares it.  libc++'s
   __fwd/byte.h: enum class byte : unsigned char; */
namespace lib {
enum class byte : unsigned char;
}
enum class E : short;
enum U : long;
static_assert(sizeof(lib::byte) == 1 && sizeof(E) == 2 && sizeof(U) == 8);
static int as_int(lib::byte b) { return (int)b; }
enum class E : short { a = 3, b };
enum class E : short;
enum U : long { u = 1L << 40 };
int main()
{
    lib::byte b = (lib::byte)200;
    return (as_int(b) != 200) + ((int)E::b != 4) * 2 + (U::u >> 40 != 1) * 4;
}
