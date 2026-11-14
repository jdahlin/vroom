/* constructors, member initializers, destructors of locals */
int live = 0;
int log_[16];
int nlog = 0;
struct Tracked {
	int id;
	Tracked(int i) : id(i) { live++; }
	~Tracked() { live--; log_[nlog++] = id; }
	int get() const { return id; }
};
struct Pair {
	int a, b;
	Pair() : a(1), b(2) { }
	Pair(int x, int y) : a(x), b(y) { }
	explicit Pair(int x) : a(x), b(x * 2) { }
	int sum() const { return a + b; }
};
struct Acc {
	long total;
	int count;
	Acc();
	Acc(long start, int n);
	void add(long v) { total += v; count++; }
	~Acc();
};
Acc::Acc() : total(0), count(0) { }
Acc::Acc(long start, int n) : total(start), count(n) { }
Acc::~Acc() { log_[nlog++] = 100 + count; }
struct Body { int v; Body(int x) { v = x + 1; } };
int scopes() {
	Tracked t1(1);
	{
		Tracked t2(2);
		if (live != 2) return -1;
	}
	if (live != 1) return -2;
	for (int i = 0; i < 3; i++) { Tracked t(10 + i); if (live != 2) return -3; }
	return live;
}
int early(int n) {
	Tracked a(7);
	if (n > 0) return a.get() + n;
	Tracked b(8);
	return b.get();
}
int main() {
	Pair p; if (p.sum() != 3) return 1;
	Pair q(4, 5); if (q.sum() != 9) return 2;
	Pair r{6, 7}; if (r.sum() != 13) return 3;
	Pair s(8); if (s.sum() != 24) return 4;
	Body bd(4); if (bd.v != 5) return 5;
	if (scopes() != 1 || live != 0) return 6;
	if (nlog != 5 || log_[0] != 2 || log_[1] != 10 || log_[2] != 11 || log_[3] != 12 || log_[4] != 1) return 7;
	nlog = 0;
	if (early(1) != 8 || live != 0 || nlog != 1 || log_[0] != 7) return 8;
	nlog = 0;
	if (early(0) != 8 || live != 0 || nlog != 2 || log_[0] != 8 || log_[1] != 7) return 9;
	nlog = 0;
	{ Acc acc; acc.add(5); acc.add(6); if (acc.total != 11) return 10; Acc other(100, 7); if (other.total != 100) return 11; }
	if (nlog != 2 || log_[0] != 107 || log_[1] != 102) return 12;
	return 0;
}
