// std: c++20
// [temp.mem.func]/1, [basic.scope.temp]/1: the out-of-class definition of a member of a class
// template names the template parameters differently from the class (libc++'s __vector_layout);
// its decl-specifiers and template-id find the definition's names, and its body the class's members
template <class T, class A>
struct C {
	using at = A;
	using tt = T;
	C(C &&o) noexcept;
	C(const at &a);
	at second() const;
	tt first() const;
	int sum() const;
	static at make();
	T x;
	A y;
};
template <class U, class V>
C<U, V>::C(C &&o) noexcept : x(o.x), y(o.y)
{
}
template <class U, class V>
C<U, V>::C(const at &a) : x(5), y(a)
{
}
template <class U, class V>
typename C<U, V>::at C<U, V>::second() const
{
	return y;
}
template <class U, class V>
U C<U, V>::first() const
{
	return x;
}
template <class U, class V>
int C<U, V>::sum() const
{
	tt a = x;
	at b = y;
	return int(a) + int(b) + int(sizeof(at));
}
template <class U, class V>
V C<U, V>::make()
{
	return 9;
}
int main()
{
	C<int, short> c(4);
	C<int, short> d(static_cast<C<int, short> &&>(c));
	if (d.second() != 4 || d.first() != 5 || d.sum() != 11 || C<int, short>::make() != 9)
		return 1;
	return 0;
}
