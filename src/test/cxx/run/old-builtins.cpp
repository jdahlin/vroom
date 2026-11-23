/* builtins that are library calls, (void) casts, casts to references */
template <class T> T &&fwd(T &x) { return static_cast<T &&>(x); }
template <class T> T &same(T &x) { return static_cast<T &>(x); }
int len(const char *s) { return (int)__builtin_strlen(s); }
int copy_sum(const int *src, int n) { int dst[8]; __builtin_memcpy(dst, src, n * sizeof(int)); int s = 0; for (int i = 0; i < n; i++) s += dst[i]; return s; }
int cmp(const char *a, const char *b) { return __builtin_memcmp(a, b, 3); }
int likely_(int x) { if (__builtin_expect(x > 3, 1)) return 1; return 0; }
bool consteval_() { return __builtin_is_constant_evaluated(); }
void fill(char *p, int n) { __builtin_memset(p, 'x', n); (void)p; ((void)0); }
int heap() { int *p = static_cast<int *>(__builtin_operator_new(4 * sizeof(int))); p[0] = 5; p[3] = 7; int r = p[0] + p[3]; __builtin_operator_delete(p); return r; }
int never(int x) { if (x) return 1; __builtin_unreachable(); }
int main() {
	if (len("hello") != 5) return 1;
	int a[4] = { 1, 2, 3, 4 };
	if (copy_sum(a, 4) != 10) return 2;
	if (cmp("abc", "abd") >= 0 || cmp("abc", "abc") != 0) return 3;
	if (likely_(5) != 1 || likely_(1) != 0) return 4;
	if (consteval_()) return 5;
	char buf[4]; fill(buf, 3); if (buf[0] != 'x' || buf[2] != 'x') return 6;
	if (heap() != 12) return 7;
	int v = 3; int &&rv = fwd(v); rv = 4; same(v) += 1; if (v != 5) return 8;
	if (never(1) != 1) return 9;
	return 0;
}
