// reference members, implicit and defaulted destructors (members and bases destroyed in turn)
static int live, order[8], n;

struct Res {
	int id;
	Res(int i) : id(i) { live++; }
	~Res() { live--; if (n < 8) order[n++] = id; }
};

struct Pair {		// no destructor declared: the implicit one destroys b, then a
	Res a, b;
	Pair(int x) : a(x), b(x + 1) {}
};

struct Wrap {
	Pair p;
	Res r;
	Wrap(int x) : p(x), r(x + 2) {}
	~Wrap() = default;
};

struct Base { Res r; Base(int x) : r(x) {} };
struct Other { Res q; Other(int x) : q(x) {} };
struct Both : Base, Other {	// two bases that need destruction
	Both(int x) : Base(x), Other(x + 1) {}
};

struct View {
	const int &ref;
	int &out;
	View(const int &r, int &o) : ref(r), out(o) {}
	void copy() const { out = ref + 1; }
	int get() const { return ref; }
};

template <class L> struct Lhs {
	L lhs;
	int tag;
	Lhs(L in, int t) : lhs(in), tag(t) {}
	bool same(int v) const { return lhs == v; }
};

static int scopes()
{
	{
		Pair p(1);

		if (live != 2)
			return 1;
	}
	if (live != 0 || n != 2 || order[0] != 2 || order[1] != 1)
		return 2;
	n = 0;
	{
		Wrap w(10);
		Both b(20);

		if (live != 5)
			return 3;
	}
	// b first: Other's q (21), Base's r (20); then w: r (12), p.b (11), p.a (10)
	if (live != 0 || n != 5 || order[0] != 21 || order[1] != 20 || order[2] != 12 || order[3] != 11 || order[4] != 10)
		return 4;
	return 0;
}

int main()
{
	int a = 5, b = 0;
	View v(a, b);
	Lhs<const int &> l(a, 1);

	v.copy();
	a = 9;
	if (b != 6 || v.get() != 9 || !l.same(9))
		return 10;
	return scopes();
}
