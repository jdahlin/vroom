// T{a, ..., {x, y}}: an element that is a braced list itself, at index 16 or later (json j_values = {..., {{"a", 1}}}
// in unit-comparison: 22 elements)
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
public:
	typedef E value_type;
	constexpr const E *begin() const { return __begin_; }
	constexpr unsigned long size() const { return __size_; }
};
}
struct E { int a, b; E(int a) : a(a), b(0) {} E(int a, int b) : a(a), b(b) {} };
struct L {
	int sum, n;
	L(std::initializer_list<E> l) : sum(0), n((int)l.size()) { for (unsigned long i = 0; i < l.size(); i++) sum += l.begin()[i].a * 10 + l.begin()[i].b; }
};
int main()
{
	L l = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, {2, 3}, {4, 5} };
	return l.n == 18 && l.sum == 160 + 23 + 45 ? 0 : 1;
}
