/* [class.mem], [expr.prim.id]/3: decltype in a member-specification:
   a non-static data member in an unevaluated operand; a dependent
   functional cast under unary plus in a static member initializer
   (libc++ <limits> numeric_limits::traps) */
struct S { int m; static const int k = 3; typedef decltype(m) A; decltype(k) b; typedef decltype((m)) e;
 static int g(); decltype(g()) h; decltype(sizeof(m)) q; };
template <class T> struct U { T m; decltype(m) a; decltype(m+1) b; decltype(+T(0)) c; typedef decltype(c) C; static constexpr bool z = __is_same(decltype(+T(0)), T); };
static_assert(__is_same(S::A, int));
static_assert(__is_same(S::e, int&));
static_assert(U<char>::z == false && U<int>::z && U<long double>::z && !U<bool>::z);
static_assert(__is_same(U<char>::C, int));
int main() { return U<int>::z ? 0 : 1; }
