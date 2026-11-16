// T({a, b}): a temporary from the list, then T(T&&) = default of a class that is not trivially copyable, called
// (json's object_t::value_type({"one", 1}), libc++'s pair(pair&&) = default): built in place, member by member
static int copies;
struct S { int v; S(int v) : v(v) {} S(const S &o) : v(o.v) { copies++; } };
struct P {
	S a; int b;
	P(int x, int y) : a(x), b(y) {}
	P(const P &) = default;
	P(P &&) = default;
};
static int take(P p) { return p.a.v * 10 + p.b; }
int main()
{
	P q = P({4, 5});
	return take(P({1, 2})) == 12 && q.a.v == 4 && q.b == 5 ? 0 : 1;
}
