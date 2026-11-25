// std: c++20
// a constructor's braced argument copy-list-initializes its parameter ([over.match.ctor],
// [expr.call]/7, [dcl.init.list]/3): an explicit constructor of the parameter's class is not a
// candidate ([over.match.list]/1), as for an ordinary call; vcc once direct-list-initialized it
// in construct, so Y y{{1}} and a mem-initializer m({1}) were accepted
struct X { explicit X(int v) : v(v) {} int v; };
struct Y { Y(X x) : v(x.v) {} int v; };
struct I { I(int a, int b) : v(a * 10 + b) {} int v; };
struct W { W(I i) : v(i.v) {} W(const I &a, I &&b) : v(a.v * 100 + b.v) {} int v; };
struct M { W m; M() : m({5, 6}) {} };
struct R { W m; R() : m({1, 2}, {3, 4}) {} };
template <class T> concept list_from_one = requires { T{{1}}; };
template <class T> concept list_from_two = requires { T{{1, 2}}; };
static_assert(!list_from_one<Y>);
static_assert(list_from_two<W>);
int main()
{
	W a{{1, 2}};
	M m;
	R r;
	if (a.v != 12 || m.m.v != 56 || r.m.v != 1234)
		return 1;
	return 0;
}
