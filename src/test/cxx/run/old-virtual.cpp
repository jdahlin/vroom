// virtual functions: vtables, calls through them, pure functions, virtual destructors
struct Shape {
	int id;
	Shape(int i) : id(i) {}
	virtual ~Shape() {}
	virtual int area() const = 0;
	virtual int sides() const { return 0; }
	int twice() const { return 2 * area(); }
};

struct Square : Shape {
	int w;
	Square(int i, int ww) : Shape(i), w(ww) {}
	int area() const override { return w * w; }
	int sides() const override { return 4; }
};

struct Rect : Square {
	int h;
	Rect(int i, int ww, int hh) : Square(i, ww), h(hh) {}
	int area() const override { return w * h; }
	virtual int extra() const { return h - w; }
};

struct Counter {
	virtual void bump(int n);
	virtual int get() const;
	int total;
	Counter() : total(0) {}
};

void Counter::bump(int n) { total += n; }
int Counter::get() const { return total; }

struct Doubler : Counter {
	void bump(int n) override { Counter::bump(2 * n); }
};

static int measure(const Shape &s) { return s.area() + s.sides(); }
static int measure_p(const Shape *s) { return s->twice() + s->id; }
static void feed(Counter &c) { c.bump(5); c.bump(1); }

static int destroyed;
struct Tracked {
	virtual ~Tracked() { destroyed += 1; }
	virtual int kind() const { return 1; }
};
struct TrackedMore : Tracked {
	~TrackedMore() override { destroyed += 10; }
	int kind() const override { return 2; }
};

static int kinds()
{
	Tracked a;
	TrackedMore b;
	const Tracked &r = b;

	return a.kind() * 10 + r.kind();
}

int main()
{
	Square sq(1, 3);
	Rect re(2, 3, 5);
	Counter c;
	Doubler d;

	if (measure(sq) != 13 || measure(re) != 19)
		return 1;
	if (measure_p(&sq) != 19 || measure_p(&re) != 32)
		return 2;
	if (re.extra() != 2)
		return 3;
	feed(c);
	feed(d);
	if (c.get() != 6 || d.get() != 12)
		return 4;
	if (kinds() != 12 || destroyed != 12)
		return 5;
	return 0;
}
