/* diag-differs: fmt.c checks printf formats only, no scanf (TODO.md: "Merged diagnostics: cases from the D8 branch") */
/* D5.6: portability between Arm Linux and Apple: typedef names in -Wformat (clang's sugar,
   %zu/%td/%jd fix-its), the <inttypes.h> macro for a 64-bit typedef, plain char's sign,
   -Wliteral-range */
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
signed char hexb = 0xfe, octb = 0376, decb = 254;
short hexs = (0xffff);
