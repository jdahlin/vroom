// std: c++20
// X::X() = default;, X::X(const X&) = default; and X::X(X&&) = default; outside the class define the
// constructors (other TUs call them: Skia's SkColorInfo); the move moves its class members
static int live;
struct sp {
	int *p;
	sp() : p() {}
	sp(int *q) : p(q) { ++*p; }
	sp(const sp &o) : p(o.p) { if (p) ++*p; }
	sp(sp &&o) : p(o.p) { o.p = nullptr; }
	~sp() { if (p) --*p; }
};
struct Base {
	int tag = 5;
};
class CI : public Base {
  public:
	CI();
	CI(int *rc, int ct);
	CI(const CI &);
	CI(CI &&);
	~CI();
	sp cs;
	int ct = 3;
};
CI::CI() = default;
CI::CI(int *rc, int c) : cs(rc), ct(c) {}
CI::CI(const CI &) = default;
CI::CI(CI &&) = default;
CI::~CI() = default;
int main()
{
	int rc = 0;
	{
		CI d;
		if (d.ct != 3 || d.tag != 5 || d.cs.p)
			return 1;
		CI a(&rc, 7);
		a.tag = 9;
		CI b(a);
		if (rc != 2 || b.ct != 7 || b.tag != 9 || b.cs.p != &rc)
			return 2;
		CI m(static_cast<CI &&>(a));
		if (rc != 2 || a.cs.p || m.cs.p != &rc || m.ct != 7 || m.tag != 9)
			return 3;
	}
	return rc != 0 ? 4 : live;
}
