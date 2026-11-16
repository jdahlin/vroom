// std: c++20
// in a const member function a local's members are not const (Skia's SkDCubic::chopAt() const
// writing dst.pts[0]); this's are, also through this->
template <class T, int N> struct Arr {
	T e[N];
	T &operator[](int i) { return e[i]; }
	const T &operator[](int i) const { return e[i]; }
	int which() { return 1; }
	int which() const { return 2; }
};
struct P { double x; };
struct Pair { Arr<P, 7> pts; };
struct C {
	Arr<P, 4> f;
	Pair chop() const;
	int kinds() const { return f.which() * 10 + this->f.which(); }
};
Pair C::chop() const
{
	Pair d;
	d.pts[0] = f[1];
	d.pts[1].x = d.pts.which();
	return d;
}
int main()
{
	C c{};
	c.f[1].x = 5;
	Pair p = c.chop();
	return p.pts[0].x == 5 && p.pts[1].x == 1 && c.kinds() == 22 ? 0 : 1;
}
