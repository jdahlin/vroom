// std: c++20
// f({...}): a braced list argument constructs its parameter (an initializer_list, a class); a class
// passed in registers converted from another class (const_iterator from iterator); a function
// template's return type typename X<T>::type mangled as written (N 1XIT_E4type E)
namespace std {
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
template <class T> struct traits { using diff = long; };
template <class I> typename traits<I>::diff dist(I a, I b) { return b - a; }
struct P { int x, y; P(int a, int b) : x(a), y(b) {} };
struct It { int *p; };
struct CIt { const int *p; CIt(It i) : p(i.p) {} };
int sum(std::initializer_list<int> l) { int s = 0; for (int v : l) s += v; return s; }
int area(P p) { return p.x * p.y; }
int at(CIt c) { return *c.p; }
int main() {
  int a[3] = {4, 5, 6};
  It i{a + 1};
  return sum({1, 2, 3}) == 6 && area({3, 4}) == 12 && at(i) == 5 && dist(a, a + 3) == 3 ? 0 : 1;
}
