// [enum.udecl]/1: the enum-name of a using-enum-declaration may be a typedef-name (or alias) that denotes an
// enumeration; its enumerators become visible (cwg2621).
enum class E { a, b };
namespace One {
using E_t = E;
using enum E_t;
static_assert(b == E::b, "");
}
typedef E E2;
namespace Two {
using enum E2;
static_assert(a == E::a, "");
}
