// x = {...} of a class: x.operator=(T{...}), the list a temporary of the parameter's class (json's
// r["compiler"] = {{"family", "clang"}, ...}); with the implicit operator=: a temporary of x's class, copied
// (json's m_value = {} of a union); T x{}, x = {} value-initialize: zeroed when the default constructor is not
// user-provided
struct P { int a, b; };
struct S {
	int sum = 0, n = 0;
	S() = default;
	S(int a, int b) : sum(a + b), n(2) {}
	S &operator=(const S &o) { sum = o.sum * 10; n = o.n; return *this; }
};
struct Q { int x = 5; int y = 6; };
union U { long l; int i; U() = default; U(int v) : i(v) {} };
long fill() { volatile long junk[16]; for (int i = 0; i < 16; i++) junk[i] = -1; return junk[3]; }
long vi() { U v{}; U w = {}; return v.l == 0 && w.l == 0; }
int main()
{
	fill();
	if (!vi())
		return 2;
	S s;
	s = {3, 4};
	Q q{1, 2};
	q = {};
	P p{7, 8};
	p = {9, 10};
	U u(3);
	u.l = -1;
	u = {};
	return s.sum == 70 && s.n == 2 && q.x == 5 && q.y == 6 && p.a == 9 && p.b == 10 && u.l == 0 ? 0 : 1;
}
