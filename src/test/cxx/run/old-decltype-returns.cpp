// std: c++17
// decltype return types as clang mangles them (DT cm cl L_Z..E fp_ ... cvv_E E), a library's own enable_if_t
// alias behind its namespace, T::name in a condition (NT_4kindE), a constructor template's prefix as a candidate
namespace std {
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> struct remove_reference { typedef T type; };
template <class T> struct remove_reference<T &> { typedef T type; };
template <class T> T &&forward(typename remove_reference<T>::type &t) noexcept { return static_cast<T &&>(t); }
}
namespace lib {
namespace detail {
template <bool B, class T = void> using enable_if_t = typename std::enable_if<B, T>::type;
template <class T> struct is_ok { static const bool value = true; };
struct put_fn { template <class J, class V> void operator()(J &j, V &&v) const { j.n += v; } };
template <class T> struct static_const { static constexpr T value{}; };
template <class J, class V, enable_if_t<is_ok<typename J::kind>::value, int> = 0> void put(J &j, V v) { j.n += v; }
}
constexpr const auto &put = detail::static_const<detail::put_fn>::value;
template <class V> struct ser {
  template <class J, class T> static auto put(J &j, T &&v) -> decltype(::lib::put(j, std::forward<T>(v)), void()) { ::lib::put(j, std::forward<T>(v)); }
};
}
struct J { typedef int kind; int n = 0; template <class T, lib::detail::enable_if_t<lib::detail::is_ok<T>::value, int> = 0> J(T v) : n(v) {} };
int main() { J j(1); lib::ser<int>::put(j, 2); lib::detail::put(j, 3); return j.n == 6 ? 0 : 1; }
