// &s of a class whose namespace has (binary) operator& templates, none viable: the built-in &, typed from the
// operand (the candidates tried had reset the expression's type: doctest's CHECK(&m == p) of a std::map)
namespace N {
struct S { int k; };
template <class T> struct V { T v; };
template <class T> V<T> operator&(const V<T> &a, const T &b) { return V<T>{a.v & b}; }
template <class T> V<T> operator&(const T &a, const V<T> &b) { return V<T>{a & b.v}; }
}
template <class L> struct EL { L lhs; };
struct D { template <class L> EL<L> operator<<(L &&o) { return EL<L>{static_cast<L &&>(o)}; } };
template <class L> L id(L &&x) { return static_cast<L &&>(x); }
int main()
{
	N::S s{4};
	auto e = D() << &s;
	N::S *p = id(&s);
	return e.lhs == &s && p == &s && e.lhs->k == 4 ? 0 : 1;
}
