// x->m through operator-> (twice), a class returned from another class by a converting
// constructor (explicit(bool) too, by register and by address), f() over f() const between
// member templates when the non-const one is defined out of class (std::map's __find_equal)
struct N { int v; int get() { return v; } };
struct P { N *p; N *operator->() { return p; } };
struct PP { P q; P operator->() { return q; } };

struct TI { int v; };
struct MI { TI t; MI(TI x) : t(x) {} };
template <class A, class B> struct Pr {
	A first; B second;
	Pr(A a, B b) : first(a), second(b) {}
	template <class U, class V> explicit(!__is_convertible(U, A)) Pr(Pr<U, V> &&o) : first(o.first), second(o.second) {}
};
Pr<TI, bool> g() { return Pr<TI, bool>(TI{5}, true); }
Pr<MI, bool> f() { return g(); }			/* by register */
struct Big { long x[4]; Big(TI t) { x[0] = t.v; x[1] = x[2] = x[3] = 0; } };
Pr<TI, long> h() { return Pr<TI, long>(TI{6}, 1); }
Pr<Big, long> k() { return h(); }			/* by address */

struct T {
	int n = 0;
	template <class K> int find(const K &v);
	template <class K> int find(const K &v) const { return const_cast<T *>(this)->find(v) + 100; }
	int run() { auto l = [this]() { return find(1); }; return l(); }
};
template <class K> int T::find(const K &v) { return ++n + v; }

int main()
{
	N n{3};
	P p{&n};
	PP pp{p};
	if (p->get() != 3 || pp->get() != 3 || pp->v != 3)
		return 1;
	if (f().first.t.v != 5 || !f().second || k().first.x[0] != 6 || k().second != 1)
		return 2;
	T t;
	const T &ct = t;
	if (t.run() != 2 || ct.find(1) != 103 || t.n != 2)
		return 3;
	return 0;
}
