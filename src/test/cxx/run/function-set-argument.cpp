// a name of functions (a function template, a template-id, an overload set) as the argument for a parameter of
// type pointer or reference to function: the function of that type, its template arguments deduced from the
// target ([over.over], [temp.deduct.funcaddr]: os << std::endl).  (A parameter of type reference to function: no
// function with one is built yet.)
struct O { int n = 0; O &operator<<(O &(*f)(O &)) { return f(*this); } O &operator<<(int) { return *this; } };
template <class T> T &bump(T &o) { o.n++; return o; }
O &plain(O &o) { o.n += 10; return o; }
O &over(O &o) { o.n += 100; return o; }
int over(int x) { return x; }
int apply(O &o, O &(*f)(O &)) { return f(o).n; }
int main()
{
	O o;
	o << plain;
	o << bump<O>;
	o << bump;
	o << over;
	int r = apply(o, bump) + apply(o, bump<O>) + apply(o, over);
	return o.n == 214 && r == 113 + 114 + 214 ? 0 : 1;
}
