/* D5.6 on Apple: long double is double, char is signed */
/* diag-flags: --target=arm64-apple-macos */
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTMAX_TYPE__ intmax_t;
int printf(const char *, ...);
int scanf(const char *, ...);
struct rec { uint64_t id; size_t len; };
int64_t total(void);
void show(int64_t x, size_t n, ptrdiff_t d, intmax_t m, struct rec *r, uint64_t *p) {
	printf("%lld %u %d %d\n", x, n, d, m);
	printf("%llx %d %lld\n", r->id, r->len, total());
	printf("%*d\n", n, 1);
	scanf("%lld", p);
}
int neg(char c) { return c == -1; }
char big = 200;
double huge = 1e400;
float tiny = 1e-46f;
long double far = 1e4000L;
_Static_assert(sizeof(long double) == 16, "binary128");
signed char hexb = 0xfe, octb = 0376, decb = 254;
short hexs = (0xffff);
