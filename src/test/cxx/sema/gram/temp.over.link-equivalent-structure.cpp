// [temp.over.link]/5: two expressions involving template parameters are
// equivalent when two definitions containing them would satisfy the ODR
// ([basic.def.odr]/13.1: the same tokens, each name the same entity).
// Parentheses are tokens: (N) + 1 is not N + 1 (clang: two templates).
template <int N> struct S {};
template <int N> int par(int (&)[N + 1]) { return 1; }
template <int N> int par(int (&)[(N) + 1]) { return 2; }
template <int I> int arg(S<I + 1>) { return 1; }
template <int I> int arg(S<(I) + 1>) { return 2; }
template <int I> int arg(S<I + 1>);
// Two variables named x: [N + x] names ::x in the first definition and
// r::x in the second, two templates.
constexpr int x = 1;
namespace r { template <int N> int f(int (&)[N + x]) { return 1; } }
namespace r { constexpr int x = 2; template <int N> int f(int (&)[N + x]) { return 2; } }
namespace r { template <int N> int f(int (&)[N + x]); }
