// f<T, S>(a, b) of templates overloaded by their parameter counts: the one with two (llvm-test-suite's Adobe-C++
// check_shifted_variable_sum); defaults count; a _BitInt(N) template (libc++'s abs) is never viable
struct Sh { static int s(int a, int b) { return a + b; } };
template <class T, class S> int ck(T r, T v) { return S::s(r, v); }
template <class T, class S> long ck(T r, T a, T b, T c) { return 100; }
template <class T> int d(T r, T a = 0, T b = 0) { return 3; }
template <class T> int d(T r) { return 1; }
inline double a(double x) { return x; }
template <class = int> inline long a(long x) { return x + 1; }
template <int N> _BitInt(N) a(_BitInt(N) x) { return x; }
int main() { return ck<int, Sh>(1, 2) == 3 && d<int>(1, 2) == 3 && a(4L) == 5 ? 0 : 1; }
