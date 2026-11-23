// std: c++20
// an out-of-class member's this is const only if the member is: not whatever the last inline
// body was (Skia's SkReduceOrder::reduce after SkDQuad::collapsed() const)
struct P { double x; };
struct L {
	P p[2];
	const P &operator[](int n) const { return p[n]; }
	P &operator[](int n) { return p[n]; }
	int which(int) const { return 1; }
	int which(int) { return 2; }
};
struct Q { bool collapsed() const { return true; } };
union U {
	int reduce(const L &line);
	int peek() const;
	L fLine;
};
int U::reduce(const L &line)
{
	fLine[0] = line[1];
	return fLine.which(0);
}
struct R { int f() const { return 0; } };
int U::peek() const { return fLine.which(0); }
int main()
{
	U u{};
	L l{{{1}, {2}}};
	if (u.reduce(l) != 2 || u.fLine.p[0].x != 2 || u.peek() != 1)
		return 1;
	return Q{}.collapsed() ? 0 : 2;
}
