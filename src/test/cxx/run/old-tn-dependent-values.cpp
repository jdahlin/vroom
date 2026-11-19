// std: c++20
// clang 23's Tn <type>: a defaulted value parameter of dependent type (enable_if_t<cond, int> = 0), its condition as
// an expression (sr, nt, oo, gt st); one after a pack keeps its place after J ... E
namespace std {	// (no library here: what the test uses of <type_traits>)
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T = void> using enable_if_t = typename enable_if<B, T>::type;
template <bool B, class T = void> using __enable_if_t = typename enable_if<B, T>::type;
template <class T> struct is_integral { static const bool value = true; };
template <class T> inline constexpr bool is_integral_v = true;
}
template <class S, class A> struct det { static const bool value = true; };
template <class S, class A> using detal = det<S, A>;
namespace q { template <class S> struct isx { static const bool value = true; }; }
template <class S, class A, std::enable_if_t<det<S, A>::value, int> = 0> void f1(S &, A &&) {}
template <class S, class A, std::enable_if_t<!det<S, A>::value || det<A, S>::value, int> = 0> void f2(S &, A &&) {}
template <class S, std::enable_if_t<q::isx<S>::value, int> = 0> void f3(S) {}
template <class S, std::enable_if_t<std::is_integral<S>::value, int> = 0> void f4(S) {}
template <class S, std::enable_if_t<std::is_integral_v<S>, int> = 0> void f5(S) {}
template <class S, typename std::enable_if<std::is_integral<S>::value || det<S, S>::value, int>::type = 0> void f6(S) {}
template <class S, std::__enable_if_t<std::is_integral<S>::value, int> = 0> void f7(S) {}
template <class S, std::enable_if_t<(sizeof(S) > 2), int> = 0> void f8(S) {}
template <class S, int = 0> void f9(S) {}
template <class S, bool B = det<S, S>::value> void f10(S) {}
int n = 0;
template <class O> void g(O &) {}
template <class O, class A, class... R, std::enable_if_t<det<O, A>::value, int> = 0> void g(O &o, A &&a, R &&...r) { n++; g(o, static_cast<R &&>(r)...); }
int main() { int i = 0; f1(i, 2); f2(i, 2); f3(i); f4(i); f5(i); f6(i); f7(i); f8(i); f9(i); f10(i); int o = 0; g(o, 1); g(o, 1, 2); return n == 3 ? 0 : 1; }
