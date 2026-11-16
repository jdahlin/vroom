// x = y of a class with no operator= of its own and a member that has one (json's udt name, a std::string in
// it): each member assigned by its own operator=, moved from a prvalue or an xvalue, copied from an lvalue
static int copies, moves;
struct S {
	int v;
	S(int v = 0) : v(v) {}
	S(const S &o) : v(o.v) {}
	S &operator=(const S &o) { v = o.v; copies++; return *this; }
	S &operator=(S &&o) { v = o.v; o.v = -1; moves++; return *this; }
	~S() {}
};
struct Name { S s; int k; Name(int v = 0) : s(v), k(v * 2) {} };
struct Person { Name name; int age; };
static Name mk(int v) { return Name(v); }
template <class T> T get(int v) { return T(v); }
int main()
{
	Person p{Name(1), 30}, q{Name(5), 40};
	p.name = mk(7);
	int a = p.name.s.v, ak = p.name.k;
	p.name = get<Name>(8);
	int b = p.name.s.v;
	Name w(9);
	p.name = static_cast<Name &&>(w);
	int c = p.name.s.v, wv = w.s.v;
	p.name = q.name;
	return a == 7 && ak == 14 && b == 8 && p.name.s.v == 5 && p.name.k == 10 && q.name.s.v == 5 && c == 9 && wv == -1 && moves == 3 && copies == 1 ? 0 : 1;
}
