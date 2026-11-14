// member() and T() value-initialize: a class without a user-provided default constructor is zeroed first
// (libc++'s basic_string: __rep_() of a union with __rep() = default); a constructor chosen for a local
// survives instantiating the class's destructor, whose body constructs things of its own
union rep {
	struct { char size; char data[23]; } s;
	struct { unsigned long cap, size; char *data; } l;
	rep() = default;
	rep(int n) { s.size = (char)n; }
};

template <class T> struct box {
	rep r;
	T extra;
	box() : r(), extra() {}
	box(const char *p) : r(2), extra() { (void)p; }
	~box() { rep tmp(1); extra = (T)tmp.s.size; }
	int size() const { return r.s.size; }
};

static void scribble()
{
	volatile char junk[64];
	for (int i = 0; i < 64; i++)
		junk[i] = (char)(i + 7);
}

static int f2() { box<int> b("xy"); return b.size(); }
static int f3() { box<int> b; return b.size() + b.extra; }
static int f4() { rep r = rep(); return r.l.cap == 0 && r.l.size == 0 && r.l.data == 0; }

int main()
{
	scribble();
	int a = f2();
	scribble();
	int b = f3();
	scribble();
	int c = f4();
	return a == 2 && b == 0 && c == 1 ? 0 : 1;
}
