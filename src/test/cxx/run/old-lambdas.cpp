// lambdas: captures by reference and by value, this, parameters, nesting, passing to templates
template <class F> static int apply(F f, int x) { return f(x); }
template <class F> static int twice(const F &f, int x) { return f(f(x)); }

struct Acc {
	int total;
	Acc() : total(0) {}
	int run(int n)
	{
		auto add = [this](int v) { total += v; };

		for (int i = 1; i <= n; i++)
			add(i);
		return total;
	}
	int scaled(int k) const
	{
		auto f = [=](int v) { return total * k + v; };

		return f(1);
	}
};

static int counters()
{
	int a = 1, b = 10;
	auto by_ref = [&]() { a += b; };
	auto by_val = [=]() { return a + b; };
	auto mixed = [a, &b](int x) { b += x; return a + b; };
	auto none = [](int x, int y) { return x * y; };

	by_ref();		// a = 11
	if (by_val() != 11 || a != 11)
		return 1;
	if (mixed(5) != 16 || b != 15)
		return 2;
	a = 100;
	if (by_val() != 11)	// the copy made when the lambda was
		return 3;
	return none(6, 7);
}

static int nested(int base)
{
	int sum = 0;
	auto outer = [&](int n) {
		auto inner = [&](int v) { sum += v + base; };

		for (int i = 0; i < n; i++)
			inner(i);
		return sum;
	};

	return outer(3);
}

int main()
{
	Acc acc;
	int k = 3;

	if (counters() != 42)
		return 1;
	if (nested(10) != 33)
		return 2;
	if (acc.run(4) != 10 || acc.scaled(2) != 21)
		return 3;
	if (apply([k](int x) { return x * k; }, 5) != 15)
		return 4;
	if (twice([&](int x) { return x + k; }, 1) != 7)
		return 5;
	return 0;
}
