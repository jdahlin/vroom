// pointers to members: data (a base's member through a derived class's pointer, null -1), member functions
// (virtual or not, const, of a base through a derived object), conversions, tests and comparisons, deduction
static int calls;
struct B { int i, j; void f() { calls += i; } virtual int v(int k) { return k + 1; } int g() const { return j; } };
struct D : B { int d; virtual int v(int k) { return k + 2; } };
int D::*di = &D::i;
int D::*dz;
int B::*bj = &B::j;
template <class T> int call(T &o, int (T::*pm)(int), int k) { return (o.*pm)(k); }
template <class T, class M> M get(const T &o, M T::*pm) { return o.*pm; }
int main() {
	D d;
	d.i = 3, d.j = 4, d.d = 5;
	void (B::*pf)() = &B::f;
	int (B::*pv)(int) = &B::v;
	int (B::*pg)() const = &B::g;
	int D::*dj = bj;
	(d.*pf)();
	B *pb = &d;
	(pb->*pf)();
	int r = 0;
	r |= d.*di != 3 || d.*dj != 4 || pb->*bj != 4 || calls != 6;
	r |= (d.*pv)(1) != 3 || (pb->*pv)(1) != 3 || (d.*pg)() != 4 || call<B>(d, pv, 10) != 12;
	r |= get<B, int>(d, &B::i) != 3;
	r |= dz != 0 || !(di != dz) || !di || !(pf == &B::f) || pf == 0 || (pv == &B::v) != true;
	dz = &D::d;
	r |= d.*dz != 5;
	pf = 0;
	if (pf)
		r |= 1;
	return r;
}
