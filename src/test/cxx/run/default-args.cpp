// default arguments: functions, member functions, constructors
static int scale = 3;
static int add(int a, int b = 10, int c = scale * 2) { return a + b + c; }
namespace n { int twice(int x = 21) { return 2 * x; } }

struct Box {
	int w, h;
	Box(int ww = 1, int hh = 2) : w(ww), h(hh) {}
	int area(int extra = 0) const { return w * h + extra; }
	static int unit(int k = 7) { return k; }
};

template <class T> struct Holder {
	T v;
	Holder(T x = T()) : v(x) {}
	T get(T plus = 5) const { return v + plus; }
};

int main()
{
	Box a, b(4), c(4, 5);
	Holder<int> h, g(10);

	if (add(1) != 17 || add(1, 2) != 9 || add(1, 2, 3) != 6)
		return 1;
	scale = 5;
	if (add(1) != 21 || n::twice() != 42 || n::twice(2) != 4)
		return 2;
	if (a.area() != 2 || b.area() != 8 || c.area(1) != 21 || Box::unit() != 7)
		return 3;
	if (h.get() != 5 || g.get(1) != 11)
		return 4;
	return 0;
}
