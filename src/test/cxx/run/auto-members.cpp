// std: c++23
// member functions with a deduced return type (a class, a class template instance, a partial
// specialization's operator()), a static operator(), an if's init-statement visible in its else, a value
// template parameter whose type is the class's member typedef, a substitution failure from
// alias<T>::member with the alias naming a non-class type
struct C {
	int v;
	auto get() const { return v; }
	const auto &ref() const { return v; }
	auto twice(const int &a) { return 2 * a; }
	auto none() {}
};
template <class T> struct A {
	T v;
	auto operator()(const T &a) const { return a + v; }
	auto get() const { return v; }
};
template <class T, class U> struct B { int k; };
template <class T> struct B<T, T> {
	A<T> a;
	auto operator()(const T &x, const T &) const { return a(x); }
};
struct R { int r; };
struct Cmp { static R operator()(const int &a, const int &b) { return R{a < b ? -1 : a > b}; } };
template <class F> int use(F f, int a, int b) { return f(a, b).r; }

int ifinit(int a) { if (int m = a * 2; m < 5) { return m; } else { return m / 2; } }

template <class Z> struct S {
	typedef unsigned long size_type;
	template <size_type N> static size_type align(size_type s) { return (s + (N - 1)) & ~(N - 1); }
	static size_type sz(size_type s) { return align<16>(s + 1) - 1; }
};

template <class T> using id = T;
template <class T, class = typename id<T>::first_type> int pick(T, int) { return 1; }
template <class T> int pick(T, long) { return 2; }

int main()
{
	C c{4};
	A<int> a{2};
	B<int, int> b{{3}};
	Cmp k;
	c.none();
	if (c.get() != 4 || c.ref() != 4 || c.twice(5) != 10)
		return 1;
	if (a(1) != 3 || a.get() != 2 || b(4, 4) != 7)
		return 2;
	if (k(1, 2).r != -1 || use(k, 3, 2) != 1)
		return 3;
	if (ifinit(1) != 2 || ifinit(10) != 10)
		return 4;
	if (S<char>::sz(20) != 31)
		return 5;
	if (pick((unsigned char)1, 0) != 2)
		return 6;
	return 0;
}
