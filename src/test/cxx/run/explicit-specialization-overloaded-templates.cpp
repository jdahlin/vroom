// an explicit specialization of one of several overloaded function templates, one of them in an inline
// namespace of the namespace that specializes it and with a dependent noexcept-specifier (swap<J>),
// and a call afterwards that takes its own template arguments (they once leaked from the declarator-id)
typedef decltype(sizeof(0)) size_t;
template <bool B, class T = void> struct enable_if { };
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T = void> using enable_if_t = typename enable_if<B, T>::type;
template <class T> struct is_integral { static constexpr bool value = false; };
template <> struct is_integral<int> { static constexpr bool value = true; };
template <class T> struct is_move_constructible { static constexpr bool value = true; };
template <class T> struct is_nothrow_move_constructible { static constexpr bool value = true; };
namespace lib { inline namespace v1 {
template <class T> using R = enable_if_t<is_move_constructible<T>::value>;
template <class T> R<T> sw(T &a, T &b) noexcept(is_nothrow_move_constructible<T>::value) { T t(static_cast<T &&>(a)); a = static_cast<T &&>(b); b = static_cast<T &&>(t); }
template <class T, size_t N, enable_if_t<is_integral<T>::value, int> = 0> void sw(T (&a)[N], T (&b)[N]) { for (size_t i = 0; i < N; i++) sw(a[i], b[i]); }
} }
struct J { int v; };
namespace lib { template <> inline void sw<J>(J &a, J &b) noexcept(is_nothrow_move_constructible<J>::value) { int t = a.v; a.v = b.v; b.v = t + 100; } }
int main() {
  int x = 1, y = 2;
  lib::sw(x, y);
  J a{3}, b{4};
  lib::sw(a, b);
  return x == 2 && y == 1 && a.v == 4 && b.v == 103 ? 0 : 1;
}
