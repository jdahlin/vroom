// std: c++20
// a placeholder for a deduced class type as the simple-type-specifier of a functional cast
// ([expr.type.conv]/1, [dcl.type.class.deduct]/1): the class template's deduction-guides
// ([temp.deduct.guide]/1) are the candidates ([over.match.class.deduct]/1.3), the best one's
// return type the deduced specialization, with braces and with parentheses, inside a function template
template <class A, class B> struct pair {
	A first;
	B second;
	pair(const A &a, const B &b) : first(a), second(b) {}
};
template <class A, class B> pair(A, B) -> pair<A, B>;

template <class T> struct box {
	T value;
	box(T v) : value(v) {}
};
template <class T> box(T) -> box<T>;
template <class T, unsigned N> box(T (&)[N]) -> box<T *>;

template <class T> int twice(T x) {
	auto p = pair{x, x};
	return p.first + p.second;
}

int main()
{
	auto p = pair{3, 4L};
	if (p.first != 3 || p.second != 4)
		return 1;
	auto q = pair(5, 6);
	if (q.first + q.second != 11)
		return 2;
	auto b = box{7};
	if (b.value != 7)
		return 3;
	int array[3] = {8, 9, 10};
	auto c = box{array};
	if (c.value[1] != 9)
		return 4;
	if (twice(21) != 42)
		return 5;
	return 0;
}
