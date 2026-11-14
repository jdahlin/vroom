// std: c++20
// an initializer_list's element that is a braced list itself: the element constructed from its
// arguments (recursively), nullptr elements as std::nullptr_t; an integer that is no constant 0 is
// no null pointer constant (f(char) does not take f(const char *))
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
using nullptr_t = decltype(nullptr);
}
struct V {
  int sum = 0, n = 0, nulls = 0;
  V(int x) : sum(x), n(1) {}
  V(std::nullptr_t) : nulls(1) {}
  V(std::initializer_list<V> l) { for (const V &v : l) sum += v.sum, n += v.n, nulls += v.nulls; }
};
int f(const char *) { return 1; }
int f(long) { return 2; }
int main() {
  V a = {1, {2, 3}, {{4}, nullptr}};
  char c = 0;
  return a.sum == 10 && a.n == 4 && a.nulls == 1 && f(c) == 2 ? 0 : 1;
}
