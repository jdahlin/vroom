// std: c++20
// a < b rewritten through operator<=>, with a constrained pack candidate (as variant's) that cannot
// match the arguments' shape: ruled out, not unknown
namespace std {
struct strong_ordering {
  signed char __value_;
  friend constexpr bool operator<(strong_ordering v, int) { return v.__value_ < 0; }
};
}
namespace n {
template <class... T> struct V { int i; };
template <class... T> requires (sizeof(T) && ...)
constexpr std::strong_ordering operator<=>(const V<T...>& x, const V<T...>& y) { return {(signed char)(x.i - y.i)}; }
template <class C> struct S { int v; };
template <class C> std::strong_ordering operator<=>(const S<C>& x, const S<C>& y) { return {(signed char)(x.v < y.v ? -1 : x.v > y.v)}; }
}
int main() { const n::S<char> a{1}, b{2}; return a < b ? 0 : 1; }
