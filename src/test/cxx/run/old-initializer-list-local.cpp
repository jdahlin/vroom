// a std::initializer_list<E> local from a braced list (const or not): its array and the list object, as
// auto v = {...} makes them (json's lexer tests: std::initializer_list<uint8_t> const v = {'t', 'r', 'u', 'e'})
// (the members libc++ names, which vcc fills in)
// std: c++17
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
	constexpr initializer_list(const E *p, unsigned long k) : __begin_(p), __size_(k) {}
public:
	constexpr initializer_list() : __begin_(nullptr), __size_(0) {}
	constexpr unsigned long size() const { return __size_; }
	constexpr const E *begin() const { return __begin_; }
	constexpr const E *end() const { return __begin_ + __size_; }
};
}
static int f1() { std::initializer_list<unsigned char> const v = {'t', 'r', 'u', 'e'}; int s = 0; for (auto c : v) s += c; return s; }
static int f2() { std::initializer_list<int> v = {1, 2, 3}; return (int)v.size(); }
int main() { return f1() == 't' + 'r' + 'u' + 'e' && f2() == 3 ? 0 : 1; }
