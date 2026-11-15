// std: c++20
// V x = {{a, b, c}} with an initializer_list<scalar> constructor: the inner list is no scalar, so not that
// constructor: the copy (or move) constructor from a V the inner list makes ([dcl.init.list]/3.10; json's
// std::vector<std::uint8_t> const bytes = {{0xCA, 0xFE, 0xBA, 0xBE}})
namespace std {	// (as libc++'s: the compiler fills in the pointer and the length)
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
struct V {
	int n = 0, first = 0, copies = 0;
	V(std::initializer_list<unsigned char> l) : n((int)l.size()), first(*l.begin()) {}
	V(const V &o) : n(o.n), first(o.first), copies(o.copies + 1) {}
};
int main() { V a = {{0xCA, 0xFE, 0xBA}}; V b{{7, 8}}; V c2 = {9, 10, 11, 12}; return a.n == 3 && a.first == 0xCA && b.n == 2 && c2.n == 4 && c2.copies == 0 ? 0 : 1; }
