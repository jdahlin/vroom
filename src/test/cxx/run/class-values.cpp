// classes by value: returned (by address when not trivial), temporaries, arguments, copy elision
static int live, copies, moves;

struct Buf {
	int v[16];
	int n;
	Buf(int nn) : n(nn) { live++; for (int i = 0; i < n; i++) v[i] = i; }
	Buf(const Buf &o) : n(o.n) { live++; copies++; for (int i = 0; i < n; i++) v[i] = o.v[i]; }
	Buf(Buf &&o) : n(o.n) { live++; moves++; for (int i = 0; i < n; i++) v[i] = o.v[i]; o.n = 0; }
	~Buf() { live--; }
	int sum() const { int s = 0; for (int i = 0; i < n; i++) s += v[i]; return s; }
	Buf twice() const;
};

Buf Buf::twice() const
{
	Buf r(2 * n);

	return r;
}

static Buf make(int n) { return Buf(n); }
static int total(Buf b) { return b.sum(); }
static int total_ref(const Buf &b) { return b.sum(); }

struct Pt {
	int x, y;
	Pt(int a, int b) : x(a), y(b) {}
	Pt plus(Pt o) const { return Pt(x + o.x, y + o.y); }
};

static Pt origin() { return Pt(1, 2); }

struct Big { long a, b, c; Big(long v) : a(v), b(v + 1), c(v + 2) {} };
static Big big(long v) { return Big(v); }

int main()
{
	{
		Buf a = make(4);

		if (a.sum() != 6 || live != 1 || copies || moves)
			return 1;
		if (total(a) != 6 || copies != 1)
			return 2;
		if (total(make(5)) != 10 || copies != 1)
			return 3;
		if (total_ref(make(3)) != 3)
			return 4;
		if (make(6).sum() != 15)
			return 40;
		Buf d = a.twice();
		if (d.sum() != 28)
			return 5;
	}
	if (live != 0)
		return 6;
	Pt p = origin().plus(Pt(10, 20));
	if (p.x != 11 || p.y != 22)
		return 7;
	Big g = big(5);
	if (g.a + g.b + g.c != 18)
		return 8;
	return 0;
}
