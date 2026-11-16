// std: c++20
// T{x} with x a T: copied or moved from x, not the initializer_list<char> constructor ([dcl.init.list]/3.2,
// std::string{std::move(s)}); a class that converts to no char is no element of an initializer_list<char>
extern "C" int printf(const char*, ...);
namespace std {
template <class E> class initializer_list {
  const E *__begin_;
  decltype(sizeof 0) __size_;
public:
  typedef E value_type;
  constexpr initializer_list() noexcept : __begin_(nullptr), __size_(0) {}
  constexpr decltype(sizeof 0) size() const noexcept { return __size_; }
  constexpr const E *begin() const noexcept { return __begin_; }
};
}
struct W { int v; };
struct Str {
  int how, n;
  Str(std::initializer_list<char> l) : how(1), n((int)l.size()) {}
  Str(const Str& o) : how(2), n(o.n) {}
  Str(Str&& o) : how(3), n(o.n) {}
  Str(W w) : how(4), n(w.v) {}
};
template <class T> T&& mv(T& t) { return static_cast<T&&>(t); }
struct P { Str name; int age; P(Str s, int a) : name{mv(s)}, age{a} {} };
int main() {
  Str a{'x', 'y'};
  Str b{a};
  Str c{mv(a)};
  Str d{W{7}};
  P p(Str{'q'}, 4);
  printf("%d %d %d %d %d %d %d %d\n", a.how, a.n, b.how, c.how, d.how, d.n, p.name.how, p.age);
}
