/* function templates: deduced and explicit arguments, overloads, templates calling templates, member templates */
template <class T> T max_(T a, T b) { return a > b ? a : b; }
template <class T> T square(T x) { return x * x; }
template <class T> T sum_squares(T a, T b) { return square(a) + square(b); }
template <class T, class U> long mix(T a, U b) { return (long)a * 10 + (long)b; }
template <class T> void bump(T &x) { x = x + 1; }
template <class T> int size_of() { return (int)sizeof(T); }
template <class T> T convert(long v) { return (T)v; }
template <class T> const T &pick(const T &a, const T &b, bool first) { return first ? a : b; }
int over(int) { return 1; }
template <class T> int over(T) { return 2; }
template <class T> int over(T *) { return 3; }
template <class T> int count_down(T n) { if (n == 0) return 0; return 1 + count_down(n - 1); }
struct P { int x; int y; };
template <class S> int total(const S &s) { return s.x + s.y; }
struct Acc { int n; template <class T> void add(T v) { n += (int)v; } template <class T> T get() const { return (T)n; } };
namespace util { template <class T> T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); } }
int main() {
	if (max_(3, 7) != 7 || max_(2.5, 1.5) != 2.5 || max_<long>(3, 9L) != 9) return 1;
	if (sum_squares(3, 4) != 25) return 2;
	if (mix(1, 2L) != 12 || mix('a', 3) != 973) return 3;
	int x = 1; bump(x); long y = 10; bump(y); if (x != 2 || y != 11) return 4;
	if (size_of<int>() != 4 || size_of<double>() != 8 || size_of<P>() != 8) return 5;
	if (convert<short>(70000) != 4464 || convert<int>(5) != 5) return 6;
	int a = 1, b = 2; if (pick(a, b, false) != 2 || pick(a, b, true) != 1) return 7;
	if (over(1) != 1 || over(1.5) != 2 || over(&a) != 3) return 8;
	if (count_down(5) != 5 || count_down(3L) != 3) return 9;
	P p; p.x = 3; p.y = 4; if (total(p) != 7) return 10;
	Acc acc; acc.n = 0; acc.add(2); acc.add(3.9); acc.add('\0'); if (acc.get<long>() != 5 || acc.get<char>() != 5) return 11;
	if (util::clamp(15, 0, 10) != 10 || util::clamp(-1.5, 0.0, 1.0) != 0.0) return 12;
	return 0;
}
