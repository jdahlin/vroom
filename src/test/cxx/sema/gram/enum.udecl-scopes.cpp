// [enum.udecl]/2: a using-enum-declaration introduces the enumerators of a
// scoped enumeration into the namespace or block scope it appears in.
enum class E { a, b };
namespace N {
using enum E;
constexpr int x = static_cast<int>(b);
}
int f()
{
    using enum E;
    return static_cast<int>(a) + static_cast<int>(b) + N::x;
}
