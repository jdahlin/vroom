// a name of overloaded functions or of a template where a function's type is wanted ([over.over]): call(g),
// call(&g), os << endl
template <class T> T tg(T x) { return x + 1; }
int g(int x) { return x; }
double g(double x) { return x * 2; }
int call(int (*p)(int)) { return p(3); }
double calld(double (&p)(double)) { return p(1.5); }
template <class T> struct O { int n; };
template <class T> O<T> &e(O<T> &o) { o.n++; return o; }
struct S : O<char> {
	S &operator<<(O<char> &(*p)(O<char> &)) { p(*this); return *this; }
	S &operator<<(int) { return *this; }
};
int main() {
	S s;
	s.n = 0;
	s << e << 1 << e;
	return call(g) + call(&g) + call(tg) + (int)calld(g) + s.n == 3 + 3 + 4 + 3 + 2 ? 0 : 1;
}
