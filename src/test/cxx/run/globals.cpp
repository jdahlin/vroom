// namespace-scope variables initialized when the program starts, in order; objects with constructors
// and destructors (run at exit, in reverse)
extern "C" void _exit(int);
static int calls;
static int next() { return ++calls; }

static int a = next();			// 1
static const int b = next() * 10;	// 20
int c = a + b;				// 21
static int k = 7;			// a constant: no code

struct Reg {
	int id, at;
	Reg(int i) : id(i), at(next()) {}
	~Reg();
};

// destroyed in reverse: d's r (made last, at 5), then r2 (at 4), then r1 (at 3)
static int ended, expect[3] = { 5, 4, 3 };
Reg::~Reg()
{

	if (at != expect[ended++])
		_exit(40);
	if (ended == 3)
		_exit(0);	// every destructor ran, in order
}

struct Dflt {
	Reg r;
	Dflt() : r(0) {}
};

static Reg r1(1);		// at 3
static Reg r2 = Reg(2);		// at 4
static Dflt d;			// at 5
static int registered = r1.id + r2.id * 10 + d.r.id * 100;	// 21

int main()
{
	if (a != 1 || b != 20 || c != 21 || k != 7)
		return 1;
	if (r1.at != 3 || r2.at != 4 || d.r.at != 5 || registered != 21 || calls != 5)
		return 2;
	return 3;	// (the destructors' _exit(0) replaces this)
}
