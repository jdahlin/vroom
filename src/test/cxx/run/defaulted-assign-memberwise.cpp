// T &operator=(const T&) = default and (T&&) = default of a class that is not trivially copyable (std::map's):
// each base and member assigned by its own operator= (moved for the move one), the object the result
struct M {
	int v = 0, how = 0;
	M() = default;
	M(const M &) = default;
	M(M &&) = default;
	M &operator=(const M &o) { v = o.v; how = 1; return *this; }
	M &operator=(M &&o) { v = o.v; o.v = -1; how = 2; return *this; }
};
struct B { M m; };
struct D : B {
	int x = 0;
	M n;
	D() = default;
	D(const D &) = default;
	D &operator=(const D &) = default;
	D &operator=(D &&) = default;
};
D mk() { D d; d.x = 9; d.n.v = 4; return d; }
int main()
{
	D a, b, c2;
	b.x = 3, b.m.v = 5, b.n.v = 6;
	a = b;
	D &r = (c2 = static_cast<D &&>(b));
	D e;
	e = mk();
	return a.x == 3 && a.m.v == 5 && a.n.v == 6 && a.n.how == 1 && c2.n.how == 2 && b.n.v == -1 && &r == &c2 && e.x == 9 && e.n.v == 4 ? 0 : 1;
}
