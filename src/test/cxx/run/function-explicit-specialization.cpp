// explicit specializations of a function template: template <> R f(P...) (arguments deduced) and f<A>(...), inline
// or not, declared first and defined later; calls with explicit arguments use them (llvm-test-suite Adobe-C++ tolerance_equal)
template <typename T> inline int te(T &a, T &b) { return 1; }
template <> inline int te(int &a, int &b) { return 2; }
template <> inline int te(double &a, double &b) { return 3; }
template <typename T> int chk(T r) { T t = r; return te<T>(r, t); }
int main1() { double d = 1; int i = 1; long l = 1; return (te(d, d) == 3 && te(i, i) == 2 && te(l, l) == 1 && chk<double>(1.0) == 3 && chk<int>(1) == 2) ? 0 : 1; }
template <typename T> int tv(T a) { return 1; }
template <> int tv(double a) { return 3; }
template <> int tv<char>(char a);
int main2() { return tv<double>(1.0) == 3 && tv(2.0) == 3 && tv(1) == 1 && tv('c') == 4 ? 0 : 1; }
template <> int tv<char>(char a) { return 4; }
int main() { return main1() || main2(); }
