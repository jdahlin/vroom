// std: c++20
// R{} in R's own members makes code generation a default constructor; R stays an aggregate, so
// R t = {x, 1, 5} still initializes its members (Skia's SkIRect: SkIRect{}.intersect(a, b) in Intersects)
struct R {
	int a = 0, b = 0, c = 5;
	bool lt(const R &x) const { return a < x.b; }
	static bool I(const R &x) { return R{}.lt(x); }
};
int main()
{
	int x = 3;
	R t = {x, 1, 5};
	R u{x, 0, 6};
	if (t.a != 3 || t.b != 1 || t.c != 5 || u.a != 3 || u.b != 0 || u.c != 6)
		return 1;
	return R::I(t) ? 0 : 2;
}
