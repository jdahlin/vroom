// [temp.mem]/1, [dcl.fct.spec]/2: a constructor template with explicit(constant-expression)
// and a defaulted template argument (libc++'s pair() with __check_pair_construction)
template <class A, class B> struct Check {
	static constexpr bool enable() { return true; }
	static constexpr bool implicit() { return true; }
};
template <class T1, class T2> struct Pair {
	T1 first; T2 second;
	template <class D = Check<T1, T2>, bool = D::enable()>
	explicit(!D::implicit()) constexpr Pair() : first(), second() {}
	Pair(T1 a, T2 b) : first(a), second(b) {}
};
int main()
{
	Pair<int, int> p;
	Pair<int, int> q(3, 4);
	return p.first + q.second - 4;
}
