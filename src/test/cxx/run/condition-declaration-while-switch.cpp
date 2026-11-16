// while (auto t = get()) and switch (int k = f()): the variable is declared and initialized by the condition,
// each iteration for while (json's binary_reader: while (auto element_type = get()) read garbage and looped)
struct R {
	const int *p; int pos;
	int get() { int v = p[pos]; pos++; return v; }
};
int main()
{
	int b[] = { 3, 2, 1, 0, 9 };
	R r{b, 0};
	int n = 0, sum = 0, odd = 0;
	while (auto t = r.get()) {
		n++;
		sum += t;
		if (t % 2 == 0)
			continue;
		odd++;
	}
	int which = 0;
	switch (int k = r.get()) {	/* (9: the element after the 0) */
	case 9: which = k + 1; break;
	default: which = -1;
	}
	return n == 3 && sum == 6 && odd == 2 && r.pos == 5 && which == 10 ? 0 : 1;
}
