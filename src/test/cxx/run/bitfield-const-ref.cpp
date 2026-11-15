// std: c++20
// const T& bound to a bit-field: a temporary holding its value (Skia's
// SkToBool(fBitfields.fAntiAlias)); the field itself is not changed through it
template <typename T> constexpr bool tb(const T &x) { return (bool)x; }
struct B { unsigned a : 3; unsigned d : 1; };
static int f(const B &b) { return tb(b.d) + tb(b.a); }
static unsigned g(B &b)
{
	const unsigned &r = b.a;
	b.a = 2;
	return r;
}
int main()
{
	B b{5, 1};
	if (f(b) != 2 || g(b) != 5 || b.a != 2)
		return 1;
	B z{0, 0};
	return f(z);
}
