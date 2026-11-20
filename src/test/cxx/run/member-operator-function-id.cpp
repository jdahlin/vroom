// std: c++20
// [expr.ref]/1, [over.oper]/1: x.operator->() and p->operator()(...): the member access names a
// member operator function by its operator-function-id, called as a function
struct P { int v; };
struct W {
	P p;
	P *operator->() { return &p; }
	const P *operator->() const { return &p; }
	int operator()(int a) const { return p.v + a; }
	int operator[](int i) const { return p.v * i; }
	W &operator+=(int a) { p.v += a; return *this; }
};
int main()
{
	W w{{5}};
	const W &c = w;
	if (w.operator->()->v != 5 || c.operator->()->v != 5)
		return 1;
	if (w.operator()(2) != 7 || (&w)->operator()(3) != 8)
		return 2;
	if (c.operator[](3) != 15)
		return 3;
	w.operator+=(4);
	if (w.p.v != 9)
		return 4;
	return 0;
}
