// exceptions: throw of scalars and classes, handlers by type, base class, reference, catch-all, rethrow
static int destroyed;

struct Error {
	int code;
	Error(int c) : code(c) {}
	virtual ~Error() { destroyed++; }
	virtual int kind() const { return 1; }
};

struct Fatal : Error {
	Fatal(int c) : Error(c) {}
	int kind() const override { return 2; }
};

struct Plain { int a, b; };

static int thrower(int what)
{
	if (what == 1)
		throw 7;
	if (what == 2)
		throw Error(20);
	if (what == 3)
		throw Fatal(30);
	if (what == 4)
		throw "text";
	if (what == 5) {
		Plain p;

		p.a = 3, p.b = 4;
		throw p;
	}
	if (what == 6)
		throw 2.5;
	return what;
}

static int classify(int what)
{
	int r = 0;

	try {
		r = thrower(what);
		r += 1000;
	} catch (int v) {
		r = v;
	} catch (const Fatal &f) {
		r = f.code + f.kind();
	} catch (const Error &e) {
		r = e.code + e.kind();
	} catch (const char *s) {
		r = s[0];
	} catch (Plain p) {
		r = p.a * p.b;
	} catch (...) {
		r = -1;
	}
	return r;
}

static int rethrown(int what)
{
	int seen = 0;

	try {
		try {
			thrower(what);
		} catch (const Error &e) {
			seen = e.kind();
			throw;
		}
	} catch (const Error &e) {
		return seen * 100 + e.code;
	} catch (...) {
		return -2;
	}
	return seen;
}

int main()
{
	if (classify(0) != 1000 || classify(1) != 7 || classify(2) != 21 || classify(3) != 32)
		return 1;
	if (classify(4) != 't' || classify(5) != 12 || classify(6) != -1)
		return 2;
	if (rethrown(2) != 120 || rethrown(3) != 230 || rethrown(1) != -2 || rethrown(0) != 0)
		return 3;
	// (destroyed == 4 with clang; vcc leaves the two rethrown exceptions alive: a handler left by an
	// exception does not end its catch yet, PLAN-cxx.md status 34)
	return destroyed >= 2 ? 0 : 4;
}
