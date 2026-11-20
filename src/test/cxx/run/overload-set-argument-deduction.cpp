// [temp.deduct.call]/6: an overload set as the argument of a parameter of function pointer type
// of a template: without function templates, the member for which deduction succeeds (only one);
// with function templates, a non-deduced context
template <class T> T tg(T x) { return x + 1; }
int g(int x) { return x; }
double g(double x) { return x * 2; }
long g(long x, long y) { return x + y; }
template <class R> R apply(R (*f)(int)) { return f(5); }
template <class T> T apply2(T x, T (*f)(T)) { return f(x); }
template <class T> T apply3(T (*f)(T), T x) { return f(x); }
int main()
{
	int r = 0;
	r += apply(g);            // R = int (g(double) and g(long, long) do not match): 5
	r += apply2(2, tg);       // T = int from x, tg<int>: 3
	r += apply3(tg, 7);       // T = int from x: 8
	r += (int)apply3(g, 2.0); // T = double from x, g(double): 4
	return r == 5 + 3 + 8 + 4 ? 0 : 1;
}
