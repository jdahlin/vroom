// x = y of a class with no operator= of its own and a member that has one (json's udt name, a std::string in
// it): each member assigned by its own operator=, moved from a prvalue or an xvalue, copied from an lvalue;
// from another type through a converting constructor (alt_string = "text")
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
struct Contact { Person p; Name addr; };	/* (implicit operator= inside implicit operator=) */
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
	Name cv;
	cv = 11;	/* (Name(int): a temporary, moved from) */
	int ccv = cv.s.v + cv.k;
	Contact x{p, Name(2)}, y{q, Name(3)};
	x = y;
	int cx = x.p.name.s.v + x.addr.s.v * 10;
	x = static_cast<Contact &&>(y);
	int cy = y.p.name.s.v;
	return ccv == 33 && cx == 35 && cy == -1 && a == 7 && ak == 14 && b == 8 && p.name.s.v == 5 && p.name.k == 10 && q.name.s.v == 5 && c == 9 && wv == -1 && moves == 6 && copies == 3 ? 0 : 1;
}
