// std: c++20
// std::initializer_list<E> x = {a, b} and x{a, b}: the list itself, its array in the block ([dcl.init.list]/5)
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
int main()
{
	std::initializer_list<unsigned char> const v = {'t', 'r', 'u', 'e'};
	std::initializer_list<int> w{1, 2, 3};
	return v.size() == 4 && v.begin()[1] == 'r' && w.size() == 3 && w.begin()[2] == 3 ? 0 : 1;
}
