// return of a class returned by address inside a try block's body (json's value(ptr, default):
// try { return ptr.get_checked(this).get<T>(); } catch (out_of_range&) { return default_value; })
static int live;
struct C {
	int v;
	C(int v) : v(v) { live++; }
	C(const C &o) : v(o.v) { live++; }
	~C() { live--; }
};
static C make(int i) { if (i < 0) throw i; return C(i * 2); }
static C get(int i, const C &dflt)
{
	try { return make(i); }
	catch (int) { return dflt; }
}
int main()
{
	C d(7);
	int a, b;
	{
		C x = get(3, d), y = get(-1, d);
		a = x.v, b = y.v;
	}
	return a == 6 && b == 7 && live == 1 ? 0 : 1;
}
