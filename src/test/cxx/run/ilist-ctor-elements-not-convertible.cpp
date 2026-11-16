// std: c++20
// C{first, last} of a container of class elements: the initializer_list constructor is not viable when an
// argument converts to no element, even through an explicit(cond) constructor template that cannot take it
// (json's ordered_map: vector<pair<const K, V>>{first, last, alloc}, pair's explicit(...) constructors)
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
public:
	typedef E value_type;
	constexpr const E *begin() const { return __begin_; }
	constexpr unsigned long size() const { return __size_; }
};
}
template <class T> struct is_int { static constexpr bool value = false; };
template <> struct is_int<int> { static constexpr bool value = true; };
template <class A, class B> struct P {
	A a; B b;
	P(A a, B b) : a(a), b(b) {}
	template <class U1, class U2> explicit(!is_int<U1>::value) P(const P<U1, U2> &o) : a(o.a), b(o.b) {}
};
struct It { int i; int operator-(const It &o) const { return i - o.i; } };
template <class E> struct V {
	int how, n;
	V(std::initializer_list<E> l) : how(1), n((int)l.size()) {}
	template <class It> V(It first, It last) : how(2), n((int)(last - first)) {}
};
int main()
{
	V<P<int, int>> v{It{0}, It{3}};
	V<P<int, int>> w{P<int, int>(7, 8)};
	return v.how == 2 && v.n == 3 && w.how == 1 && w.n == 1 ? 0 : 1;
}
