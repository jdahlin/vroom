// std: c++20
// the implicit default constructor, copy and move constructors and destructor of a class with an
// array member: each element in turn, constructed first to last ([class.base.init]/9,
// [class.copy.ctor]/14: "each element is direct-initialized with the corresponding subobject of
// x") and destroyed last to first ([class.dtor]/13); one loop, not a call per element; and the
// default construction in a constant expression
extern "C" int printf(const char *, ...);
static int made, log_[64], logged;
struct E {
	int id, from;
	E() : id(made++), from(-1) { log_[logged++] = id; }
	E(const E &o) : id(made++), from(o.id) { log_[logged++] = 100 + o.id; }
	E(E &&o) : id(made++), from(o.id) { log_[logged++] = 200 + o.id; }
	~E() { log_[logged++] = 300 + id; }
};
struct S { int before; E a[3]; int after; };
struct L { E big[1000]; };
struct C {
	int v, n;
	constexpr C() : v(7), n(0) {}
};
struct K { C c[4]; };
constexpr int sum(const K &k)
{
	int s = 0;
	for (int i = 0; i < 4; i++)
		s += k.c[i].v;
	return s;
}
constexpr K k0;
static_assert(sum(k0) == 28);
static int check(const int *want, int n)
{
	if (logged != n)
		return 1;
	for (int i = 0; i < n; i++)
		if (log_[i] != want[i])
			return 1;
	logged = 0;
	return 0;
}
int main()
{
	{
		S s;
		static const int built[] = {0, 1, 2};
		if (check(built, 3))
			return 1;
		S t(s);
		static const int copied[] = {100, 101, 102};
		if (check(copied, 3) || t.a[2].from != 2)
			return 2;
		S u(static_cast<S &&>(s));
		static const int moved[] = {200, 201, 202};
		if (check(moved, 3) || u.a[0].from != 0)
			return 3;
	}
	static const int destroyed[] = {308, 307, 306, 305, 304, 303, 302, 301, 300};
	if (check(destroyed, 9))
		return 4;
	{
		L *l = new L;
		if (l->big[999].id != 1008)
			return 5;
		delete l;
	}
	return made == 2009 ? 0 : 6;
}
