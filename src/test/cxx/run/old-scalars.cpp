/* namespaces, for/do, ?:, casts, pointers, arrays, globals, comma, sizeof */
namespace math {
int square(int x) { return x * x; }
namespace detail { long twice(long x) { return x + x; } }
long quad(long x) { return detail::twice(detail::twice(x)); }
}
int counter = 3;
long table[4] = { 10, 20, 30, 40 };
double scale = 1.5;
int bump() { counter = counter + 1; return counter; }
int sum_for(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }
int sum_do(int n) { int s = 0; int i = 0; do { s += i; ++i; } while (i < n); return s; }
int max3(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
int trunc_to_int(double d) { return static_cast<int>(d); }
long widen(int x) { return (long)x * 100000; }
unsigned char low(int x) { return (unsigned char)x; }
double ratio(int a, int b) { return double(a) / b; }
void store(int *p, int v) { *p = v; }
int load(const int *p) { return *p; }
int sum_array(const long *p, int n) { long s = 0; for (int i = 0; i < n; ++i) s += p[i]; return (int)s; }
int local_array() { int a[5]; for (int i = 0; i < 5; i++) a[i] = i * i; int s = 0; for (int i = 0; i < 5; i++) s += a[i]; return s; }
int swap_and_diff(int *a, int *b) { int t = *a; *a = *b; *b = t; return *a - *b; }
int nested(int n) { int c = 0; for (int i = 0; i < n; i++) for (int j = 0; j <= i; j++) { if (j == 2) continue; if (i == 5) break; c++; } return c; }
int comma_expr(int x) { int y = (x += 2, x * 3); return y; }
int sizes() { return (int)(sizeof(int) + sizeof(long) + sizeof(char) + sizeof(double)); }
int strlen_(const char *s) { int n = 0; while (s[n]) n++; return n; }
int main() {
	if (math::square(7) != 49) return 1;
	if (math::quad(3) != 12) return 2;
	if (bump() != 4 || counter != 4) return 3;
	if (sum_for(10) != 45) return 4;
	if (sum_do(10) != 45) return 5;
	if (max3(3, 9, 5) != 9 || max3(9, 3, 5) != 9 || max3(1, 2, 3) != 3) return 6;
	if (trunc_to_int(3.9) != 3) return 7;
	if (widen(30000) != 3000000000L) return 8;
	if (low(0x1234) != 0x34) return 9;
	if (ratio(1, 4) != 0.25) return 10;
	int x = 0; store(&x, 42); if (load(&x) != 42) return 11;
	if (sum_array(table, 4) != 100) return 12;
	if (local_array() != 30) return 13;
	int p = 10, q = 3; if (swap_and_diff(&p, &q) != -7 || p != 3 || q != 10) return 14;
	if (nested(8) != 25) return 15;
	if (comma_expr(1) != 9) return 16;
	if (sizes() != 21) return 17;
	if (strlen_("hello") != 5) return 18;
	if (scale * 2 != 3.0) return 19;
	return 0;
}
