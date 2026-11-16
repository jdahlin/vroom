// std: c++20
// [dcl.init.aggr]/6.1: an element not explicitly initialized that has a default member initializer is
// initialized from it, a local's and a global's, a class member's through its constructor
struct M {
	int v;
	M(int x) : v(x * 2) {}
};
struct R {
	int a = 0, b = 7;
	M m = 4;
	int c = 9;
};
R gr{5};
static int f(int x)
{
	R u{x};
	R w = {x, 1};
	return u.a + u.b + u.m.v + u.c + w.b * 100;
}
int main()
{
	if (f(1) != 1 + 7 + 8 + 9 + 100)
		return 1;
	return gr.a == 5 && gr.b == 7 && gr.m.v == 8 && gr.c == 9 ? 0 : 2;
}
