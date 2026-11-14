// std: c++20
// A function template whose one parameter is an unnamed pack, called with no template argument (the pack is
// empty: libc++'s template <int&...> static constexpr bool __enable_default()), folded as a constant where
// one is needed (a defaulted template argument's enable_if).
template <bool B, class T = void> struct ei {};
template <class T> struct ei<true, T> { using type = T; };
template <class A, class B> struct chk { template <int &...> static constexpr bool en() { return true; } };
template <class A, class B> struct P {
	A first;
	B second;
	template <class C = chk<A, B>, typename ei<C::en(), int>::type = 0> constexpr P() : first(), second() {}
};
int main()
{
	P<long, int> p;
	return p.first != 0 || p.second != 0;
}
