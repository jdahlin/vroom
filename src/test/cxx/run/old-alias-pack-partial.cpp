// partial specializations whose pattern is an alias template over a pack, packs deduced from inside a
// parameter's type (get<I>(tuple<Ts...>&)), mem-initializers expanded per pack element (tuple's leaves)
template <class... T> struct tl {};
template <class... T> using al = tl<T...>;
template <class... T> using al2 = tl<int, T...>;
template <class S> struct impl { static const int v = 1; };
template <class... T> struct impl<al<T...>> { static const int v = 2; };
template <class S> struct impl2 { static const int v = 1; };
template <class... T> struct impl2<al2<T...>> { static const int v = 3; };

template <class T> T &&fwd(T &t) { return static_cast<T &&>(t); }

template <unsigned long I, class T> struct leaf {
	T v;
	template <class A> leaf(A &&a) : v(fwd(a)) {}
};
template <class Ix, class... T> struct impl_t;
template <unsigned long... I> struct seq {};
template <unsigned long... I, class... T> struct impl_t<seq<I...>, T...> : leaf<I, T>... {
	template <class... A> impl_t(A &&...a) : leaf<I, T>(fwd(a))... {}
};
template <class... T> struct tup { int a[sizeof...(T) + 1]; };
template <int I, class... T> int g(tup<T...> &t) { return sizeof(t) + I; }

int main() {
	if (impl<tl<int, long>>::v != 2 || impl<int>::v != 1)
		return 1;
	if (impl2<tl<int, long>>::v != 3 || impl2<tl<long>>::v != 1)
		return 2;
	impl_t<seq<0, 1>, int, long> t(5, 7L);
	if (static_cast<leaf<0, int> &>(t).v != 5 || static_cast<leaf<1, long> &>(t).v != 7)
		return 3;
	tup<int, long> u;
	if (g<1>(u) != 13)
		return 4;
	return 0;
}
