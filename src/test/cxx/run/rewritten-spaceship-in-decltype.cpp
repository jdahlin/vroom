// std: c++20
// guessed-names: 1
/* (le is static: vcc does not mangle a -> decltype(...) return type yet) */
extern "C" int printf(const char*, ...);
namespace std {
struct strong_ordering {
  signed char __value_;
  static const strong_ordering less, equal, greater;
  friend constexpr bool operator<=(strong_ordering a, decltype(nullptr)) { return a.__value_ <= 0; }
  friend constexpr bool operator<(strong_ordering a, decltype(nullptr)) { return a.__value_ < 0; }
  friend constexpr bool operator>=(strong_ordering a, decltype(nullptr)) { return a.__value_ >= 0; }
  friend constexpr bool operator>(strong_ordering a, decltype(nullptr)) { return a.__value_ > 0; }
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, strong_ordering::greater{1};
}
template<class T> T&& declval();
template<class T> struct W { T p; };
template<class A, class B> constexpr std::strong_ordering operator<=>(const W<A>& x, const W<B>& y) {
  return x.p < y.p ? std::strong_ordering::less : x.p == y.p ? std::strong_ordering::equal : std::strong_ordering::greater; }
struct N { int p; };	/* no comparison at all */
template<class L, class R> static auto le(L&& l, R&& r) -> decltype((void)(declval<L>() <= declval<R>()), 1) { return l <= r ? 1 : 0; }
static int le(...) { return 7; }	/* taken when l <= r is ill-formed */
int main() { W<int> a{1}, b{2}; N n{0}; printf("%d %d %d %d\n", le(a, b), le(b, a), le(n, n), (int)(a <= b)); }
