// declarations as conditions, temporaries in branches and conditions
static int live, made;

struct Guard {
	int ok;
	Guard(int o) : ok(o) { live++; made++; }
	~Guard() { live--; }
	explicit operator bool() const { return ok != 0; }
	int value() const { return ok; }
};

static Guard make(int o) { return Guard(o); }

static int sections(int a, int b)
{
	int r = 0;

	if (const Guard &g = Guard(a)) {
		if (live != 1)
			return -1;
		r += g.value();
	}
	if (live != 0)
		return -2;
	if (Guard g = make(b)) {
		r += 10 * g.value();
		if (const Guard &h = Guard(0))
			r += 1000;
		else
			r += 100;
	}
	if (live != 0)
		return -3;
	return r;
}

static int branches(int x)
{
	int r = 0;

	if (x > 0)
		r = make(x).value();
	else
		r = make(-x).value() + 100;
	if (live != 0)
		return -1;
	for (int i = 0; i < 3; i++)
		r += make(i).value();
	if (live != 0)
		return -2;
	if (make(x).value() == x)
		r += 1000;
	return live == 0 ? r : -3;
}

int main()
{
	if (sections(3, 4) != 143 || sections(0, 0) != 0)
		return 1;
	if (branches(5) != 1008 || branches(-5) != 1108)
		return 2;
	return made == 15 ? 0 : 3;
}
