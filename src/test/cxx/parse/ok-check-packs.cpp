// Function templates with a parameter pack: deduction, the body with the pack bound, f(args...) in it.
template<class T> struct rr { typedef T type; };
template<class T> struct rr<T &> { typedef T type; };
template<class T> T &&fwd(typename rr<T>::type &t) noexcept { return static_cast<T &&>(t); }
struct P { P(int, long) { } P() { } int n; };
struct Q { Q(const P &, char) { } };
template<class T, class... Args> T make(Args &&... args) { return T(fwd<Args>(args)...); }
template<class... Args> long count(Args &&... args) { return sizeof...(Args); }
template<class T, class... Rest> T first(T a, Rest... rest) { return a; }
template<class... Args> char pick(int, Args &&...);
long pick(...);
struct V {
	template<class... Args> P &emplace(Args &&... args) { p = P(fwd<Args>(args)...); return p; }
	P p;
};
void use(V v, P p, int i, long l)
{
	static_assert(sizeof(make<P>(i, l).n) == 4, "");		// T explicit, Args deduced
	static_assert(sizeof(make<P>().n) == 4, "");			// an empty pack
	static_assert(sizeof(count(i, l, p)) == 8 && sizeof(count()) == 8, "");
	static_assert(sizeof(first(l, i, p)) == 8 && sizeof(first('c')) == 1, "");
	static_assert(sizeof(pick(i, l, p)) == 1, "");			// the pack overload over f(...)
	static_assert(sizeof(v.emplace(i, l).n) == 4, "");		// a member template with a pack
	Q q = make<Q>(p, 'x');
}
