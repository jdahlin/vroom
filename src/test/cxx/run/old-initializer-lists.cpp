// std: c++20
// std::initializer_list constructors from a braced list: T x = {...} and T x{...}; the list's array of
// scalars, of classes (constructed from each element, destroyed after the constructor); constants of
// 128-bit types through ~ and casts
namespace std {	// (as libc++'s: the compiler fills in the pointer and the length)
template <class E> class initializer_list {
  const E *__begin_;
  decltype(sizeof 0) __size_;
public:
  constexpr initializer_list() noexcept : __begin_(nullptr), __size_(0) {}
  constexpr decltype(sizeof 0) size() const noexcept { return __size_; }
  constexpr const E *begin() const noexcept { return __begin_; }
  constexpr const E *end() const noexcept { return __begin_ + __size_; }
};
}
static int live = 0;
struct E { int v; E(int x) : v(x) { live++; } E(const E &o) : v(o.v) { live++; } ~E() { live--; } };
struct S { long sum = 0; long n = 0; S(std::initializer_list<int> l) { for (int x : l) sum += x; n = (long)l.size(); } };
struct T { int sum = 0; T(std::initializer_list<E> l, int k = 100) { for (const E &e : l) sum += e.v; sum += k; } };
struct U { int a, b; U(int x, int y) : a(x), b(y) {} };
constexpr int bits = __builtin_popcountg(static_cast<unsigned __int128>(~(unsigned __int128)0));
int main() {
  S s = {1, 2, 3, 4};
  S s2{5, 6};
  T t = {7, 8};
  int during = live;
  U u = {9, 10};
  return (s.sum == 10 && s.n == 4 && s2.sum == 11 && t.sum == 115 && during == 0 && live == 0 && u.a + u.b == 19 && bits == 128) ? 0 : 1;
}
