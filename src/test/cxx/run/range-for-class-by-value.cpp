// for (T x : r) with x a class by value: initialized from each element (a copy of an lvalue, or the prvalue
// itself), destroyed each iteration (json's for (auto j : values))
int made, gone;
struct E { int v; E(int x) : v(x) { made++; } E(const E &o) : v(o.v) { made++; } ~E() { gone++; } };
struct It {
	int i;
	E operator*() const { return E(i); }
	It &operator++() { ++i; return *this; }
	bool operator!=(const It &o) const { return i != o.i; }
};
struct R { It begin() const { return It{1}; } It end() const { return It{4}; } };
struct H { E a = E(5); const E *begin() const { return &a; } const E *end() const { return &a + 1; } };
int main()
{
	int s = 0;
	{
		R r;
		for (E x : r)
			s += x.v;
		H h;
		for (auto x : h)
			s += x.v * 10;
	}
	return s == 56 && made == gone ? 0 : 1;
}
