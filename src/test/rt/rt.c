/*
 * rt.c: a tiny freestanding libc for running C test programs bare metal
 * under qemu-system-aarch64 -M virt (see start.S, link.ld).  Always built by
 * clang, linked with both the vcc and the clang build of a test, so output
 * differences come from the compiler under test only.
 *
 * stdout and stderr both go to the PL011 UART (tcc's tests compare 2>&1);
 * All symbols are made weak (llvm-objcopy --weaken): a test may define its
 * own abs, div, ...
 * exit(n) leaves qemu with status n via semihosting; abort() is exit(134).
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

typedef struct _rt_file { int fd; } FILE;
static FILE f_in = { 0 }, f_out = { 1 }, f_err = { 2 };
FILE *stdin = &f_in, *stdout = &f_out, *stderr = &f_err;
int errno;

/* ---- output ---- */
#ifdef __x86_64__
/* x86-64 Linux (start-x86.S): stdout through write(2), buffered; __rt_flush at exit */
static char obuf[4096];
static unsigned on;
void __rt_flush(void)
{
	long r;
	unsigned k = 0;

	while (k < on) {
		__asm__ volatile("syscall" : "=a"(r) : "a"(1L), "D"(1L), "S"(obuf + k), "d"((long)(on - k)) : "rcx", "r11", "memory");
		if (r <= 0) break;
		k += (unsigned)r;
	}
	on = 0;
}
static void outc(int c) { obuf[on++] = (char)c; if (on == sizeof obuf) __rt_flush(); }
#else
static volatile unsigned int *const uart = (unsigned int *)0x09000000;
static void outc(int c) { *uart = (unsigned char)c; }
#endif

/* ---- string ---- */
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *mempcpy(void *d, const void *s, size_t n) { return (char *)memcpy(d, s, n) + n; }
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = (char)c; return d; }
void bzero(void *d, size_t n) { memset(d, 0, n); }
void *memmove(void *d, const void *s, size_t n)
{
	char *a = d; const char *b = s;
	if (a < b) while (n--) *a++ = *b++;
	else while (n--) a[n] = b[n];
	return d;
}
int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
	return 0;
}
int bcmp(const void *x, const void *y, size_t n) { return memcmp(x, y, n); }
void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
	return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t m) { size_t n = 0; while (n < m && s[n]) n++; return n; }
int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
		if (!*a) return 0;
	}
	return 0;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int strcasecmp(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b)) a++, b++;
	return lower((unsigned char)*a) - lower((unsigned char)*b);
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) continue; return r; }
char *stpcpy(char *d, const char *s) { while ((*d = *s++)) d++; return d; }
char *strncpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++) d[i] = s[i];
	for (; i < n; i++) d[i] = 0;
	return d;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n)
{
	char *e = d + strlen(d);
	while (n-- && *s) *e++ = *s++;
	*e = 0;
	return d;
}
char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c) return (char *)s;
		if (!*s) return 0;
	}
}
char *strrchr(const char *s, int c)
{
	const char *r = 0;
	for (;; s++) {
		if (*s == (char)c) r = s;
		if (!*s) return (char *)r;
	}
}
char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
	return l ? 0 : (char *)h;
}
size_t strspn(const char *s, const char *a) { size_t n = 0; while (s[n] && strchr(a, s[n])) n++; return n; }
size_t strcspn(const char *s, const char *a) { size_t n = 0; while (s[n] && !strchr(a, s[n])) n++; return n; }
char *strpbrk(const char *s, const char *a) { s += strcspn(s, a); return *s ? (char *)s : 0; }
char *strerror(int e) { (void)e; return "error"; }
int ffs(int x) { return __builtin_ffs(x); }
size_t wcslen(const int *s) { size_t n = 0; while (s[n]) n++; return n; }

/* ---- ctype ---- */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (lower(c) >= 'a' && lower(c) <= 'f'); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return lower(c); }

/* ---- heap: bump allocator below the top of the 256 MB of RAM ---- */
extern char __heap_start[];
static char *brk_ = __heap_start;
#define HEAP_END ((char *)0x40000000 + (240u << 20))
void *malloc(size_t n)
{
	size_t *p;
	n = (n + 15) & ~(size_t)15;
	if ((size_t)(HEAP_END - brk_) < n + 16) return 0;
	p = (size_t *)(brk_ + 8);
	p[-1] = n;
	brk_ += n + 16;
	return p;
}
void free(void *p) { (void)p; }
void *calloc(size_t a, size_t b) { void *p = malloc(a * b); if (p) memset(p, 0, a * b); return p; }
void *realloc(void *o, size_t n)
{
	void *p = malloc(n);
	if (p && o) { size_t m = ((size_t *)o)[-1]; memcpy(p, o, m < n ? m : n); }
	return p;
}
char *strdup(const char *s) { size_t n = strlen(s) + 1; return memcpy(malloc(n), s, n); }

/* ---- exit ---- */
extern void _exit(int) __attribute__((noreturn));
static void (*atx[32])(void);
static int natx;
int atexit(void (*f)(void)) { if (natx == 32) return -1; atx[natx++] = f; return 0; }
void exit(int c)
{
	while (natx) atx[--natx]();
	_exit(c);
}
void abort(void) { _exit(134); }
void __rt_assert(const char *e, const char *f, int l, const char *fn);
int fprintf(FILE *, const char *, ...);
void __rt_assert(const char *e, const char *f, int l, const char *fn)
{
	fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", f, l, fn, e);
	abort();
}
char *getenv(const char *n) { (void)n; return 0; }
static unsigned long rnd = 1;
int rand(void) { rnd = rnd * 1103515245 + 12345; return (int)((rnd >> 16) & 0x7fffffff); }
void srand(unsigned s) { rnd = s; }

/* ---- numbers ---- */
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
div_t div(int a, int b) { div_t r = { a / b, a % b }; return r; }
ldiv_t ldiv(long a, long b) { ldiv_t r = { a / b, a % b }; return r; }
unsigned long long strtoull(const char *s, char **e, int base)
{
	unsigned long long v = 0;
	int neg = 0;
	while (isspace(*s)) s++;
	if (*s == '-' || *s == '+') neg = *s++ == '-';
	if ((base == 0 || base == 16) && s[0] == '0' && lower(s[1]) == 'x') { s += 2; base = 16; }
	else if (base == 0) base = *s == '0' ? 8 : 10;
	for (;; s++) {
		int d = isdigit(*s) ? *s - '0' : isalpha(*s) ? lower(*s) - 'a' + 10 : 99;
		if (d >= base) break;
		v = v * base + d;
	}
	if (e) *e = (char *)s;
	return neg ? -v : v;
}
long long strtoll(const char *s, char **e, int b) { return (long long)strtoull(s, e, b); }
long strtol(const char *s, char **e, int b) { return (long)strtoull(s, e, b); }
unsigned long strtoul(const char *s, char **e, int b) { return (unsigned long)strtoull(s, e, b); }
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
long long atoll(const char *s) { return strtoll(s, 0, 10); }
double strtod(const char *s, char **e)
{
	double v = 0, f = 1;
	int neg = 0;
	while (isspace(*s)) s++;
	if (*s == '-' || *s == '+') neg = *s++ == '-';
	for (; isdigit(*s); s++) v = v * 10 + (*s - '0');
	if (*s == '.') for (s++; isdigit(*s); s++) v += (*s - '0') * (f /= 10);
	if (lower(*s) == 'e') {
		int x = (int)strtol(s + 1, (char **)&s, 10);
		for (; x > 0; x--) v *= 10;
		for (; x < 0; x++) v /= 10;
	}
	if (e) *e = (char *)s;
	return neg ? -v : v;
}
double atof(const char *s) { return strtod(s, 0); }

/* ---- 128-bit division (libgcc's; clang only calls these when it cannot fold) ---- */
typedef unsigned __int128 u128_t;
static u128_t udivmod128(u128_t n, u128_t d, u128_t *rem)
{
	u128_t q = 0, r = 0;
	for (int i = 127; i >= 0; i--) {
		r = r << 1 | ((n >> i) & 1);
		if (r >= d) { r -= d; q |= (u128_t)1 << i; }
	}
	if (rem) *rem = r;
	return q;
}
u128_t __udivti3(u128_t a, u128_t b) { return udivmod128(a, b, 0); }
u128_t __umodti3(u128_t a, u128_t b) { u128_t r; udivmod128(a, b, &r); return r; }
__int128 __divti3(__int128 a, __int128 b)
{
	u128_t q = udivmod128(a < 0 ? -(u128_t)a : (u128_t)a, b < 0 ? -(u128_t)b : (u128_t)b, 0);
	return (a < 0) != (b < 0) ? -(__int128)q : (__int128)q;
}
__int128 __modti3(__int128 a, __int128 b)
{
	u128_t r;
	udivmod128(a < 0 ? -(u128_t)a : (u128_t)a, b < 0 ? -(u128_t)b : (u128_t)b, &r);
	return a < 0 ? -(__int128)r : (__int128)r;
}

/* ---- math (just what the tests use) ---- */
double fabs(double x) { return __builtin_fabs(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
/* no compiler-rt for aarch64-linux here: no long double arithmetic in rt */
long double fabsl(long double x) { union { long double d; unsigned __int128 u; } v = { x }; v.u &= ~((unsigned __int128)1 << 127); return v.d; }
static double ld2d(long double x)
{
	union { long double d; unsigned __int128 u; } v = { x };
	union { double d; unsigned long long u; } r;
	unsigned long long s = (unsigned long long)(v.u >> 127), m = (unsigned long long)(v.u >> 60) & ((1ull << 52) - 1);
	int e = (int)(v.u >> 112) & 0x7fff;
	if (e == 0x7fff) e = 0x7ff;
	else if (e) { e = e - 16383 + 1023; if (e >= 0x7ff) e = 0x7ff, m = 0; else if (e <= 0) e = 0, m = 0; }
	r.u = s << 63 | (unsigned long long)e << 52 | m;
	return r.d;
}
/* not __builtin_sqrt: under -fno-builtin it is a call to sqrt, this function */
#ifdef __x86_64__
double sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
float sqrtf(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
#else
double sqrt(double x) { double r; __asm__("fsqrt %d0, %d1" : "=w"(r) : "w"(x)); return r; }
float sqrtf(float x) { float r; __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x)); return r; }
#endif
double floor(double x) { return __builtin_floor(x); }
double ceil(double x) { return __builtin_ceil(x); }
double trunc(double x) { return __builtin_trunc(x); }
double round(double x) { return __builtin_round(x); }
double copysign(double x, double y) { return __builtin_copysign(x, y); }
double fmod(double x, double y) { return x - __builtin_trunc(x / y) * y; }
double ldexp(double x, int e) { for (; e > 0; e--) x *= 2; for (; e < 0; e++) x /= 2; return x; }

/* ---- a small libm: range reduction and series, double accuracy for the tests ---- */
#define RT_LN2 0.69314718055994530942
#define RT_PI 3.14159265358979323846
double frexp(double x, int *e)
{
	union { double d; unsigned long long u; } v = { x };
	int k = (int)(v.u >> 52 & 0x7ff);
	*e = 0;
	if (x == 0 || k == 0x7ff) return x;
	if (!k) { v.d = x * 0x1p64; k = (int)(v.u >> 52 & 0x7ff) - 64; }
	*e = k - 1022;
	v.u = (v.u & ~(0x7ffull << 52)) | 1022ull << 52;
	return v.d;
}
double exp(double x)
{
	if (x != x) return x;
	if (x > 709.8) return __builtin_inf();
	if (x < -745.2) return 0;
	double k = __builtin_round(x / RT_LN2), r = (x - k * 0.6931471803691238) - k * 1.9082149292705877e-10, t = 1, s = 1;
	for (int i = 1; i < 30; i++) { t *= r / i; s += t; }
	return ldexp(s, (int)k);
}
double log(double x)
{
	int e;
	if (x != x || x < 0) return __builtin_nan("");
	if (x == 0) return -__builtin_inf();
	if (x == __builtin_inf()) return x;
	double m = frexp(x, &e);
	if (m < 0.70710678118654752) m *= 2, e--;
	double z = (m - 1) / (m + 1), z2 = z * z, t = z, s = 0;
	for (int i = 1; i < 60; i += 2) { s += t / i; t *= z2; }
	return 2 * s + e * RT_LN2;
}
double log10(double x) { return log(x) / 2.30258509299404568402; }
double pow(double x, double y)
{
	if (y == 0) return 1;
	if (y == __builtin_trunc(y) && __builtin_fabs(y) < 1e9) {
		long long n = (long long)y;
		double r = 1, b = x;
		for (unsigned long long u = n < 0 ? -(unsigned long long)n : (unsigned long long)n; u; u >>= 1, b *= b)
			if (u & 1) r *= b;
		return n < 0 ? 1 / r : r;
	}
	if (x < 0) return __builtin_nan("");
	if (x == 0) return y > 0 ? 0 : __builtin_inf();
	return exp(y * log(x));
}
/* x reduced to [-pi/4, pi/4], *q: the quadrant */
static double rt_reduce(double x, int *q)
{
	double k = __builtin_round(x / (RT_PI / 2));
	*q = (int)((long long)k & 3);
	return (x - k * 1.5707963267341256) - k * 6.077100506506192e-11;
}
static double rt_sin(double r) { double t = r, s = r; for (int i = 1; i < 14; i++) { t *= -r * r / ((2 * i) * (2 * i + 1)); s += t; } return s; }
static double rt_cos(double r) { double t = 1, s = 1; for (int i = 1; i < 14; i++) { t *= -r * r / ((2 * i - 1) * (2 * i)); s += t; } return s; }
double sin(double x) { int q; double r = rt_reduce(x, &q); return q == 0 ? rt_sin(r) : q == 1 ? rt_cos(r) : q == 2 ? -rt_sin(r) : -rt_cos(r); }
double cos(double x) { int q; double r = rt_reduce(x, &q); return q == 0 ? rt_cos(r) : q == 1 ? -rt_sin(r) : q == 2 ? -rt_cos(r) : rt_sin(r); }
double tan(double x) { return sin(x) / cos(x); }
double atan(double x)
{
	if (x != x) return x;
	if (x < 0) return -atan(-x);
	if (x > 1) return RT_PI / 2 - atan(1 / x);
	if (x > 0.41421356237309505) return RT_PI / 4 + atan((x - 1) / (x + 1));
	double t = x, s = 0;
	for (int i = 1; i < 80; i += 2) { s += t / i; t *= -x * x; }
	return s;
}
double atan2(double y, double x)
{
	if (x > 0) return atan(y / x);
	if (x < 0) return y >= 0 ? atan(y / x) + RT_PI : atan(y / x) - RT_PI;
	return y > 0 ? RT_PI / 2 : y < 0 ? -RT_PI / 2 : 0;
}
double asin(double x) { return atan2(x, sqrt((1 - x) * (1 + x))); }
double acos(double x) { return RT_PI / 2 - asin(x); }
double sinh(double x)
{
	if (__builtin_fabs(x) < 0.5) { double t = x, s = x; for (int i = 1; i < 12; i++) { t *= x * x / ((2 * i) * (2 * i + 1)); s += t; } return s; }
	double e = exp(x);
	return (e - 1 / e) / 2;
}
double cosh(double x) { double e = exp(x); return (e + 1 / e) / 2; }
double tanh(double x)
{
	if (__builtin_fabs(x) > 20) return x > 0 ? 1 : -1;
	if (__builtin_fabs(x) < 0.5) return sinh(x) / cosh(x);
	double e = exp(2 * x);
	return (e - 1) / (e + 1);
}

/* Complex division normalizes the denominator before squaring it.  The
 * exceptional cases follow the C complex arithmetic rules also used by
 * compiler-rt.  Keep scaling freestanding: neither logb nor scalbn is
 * available in the bare test environment.  Negative scaling is split above
 * the normal/subnormal boundary so only the final multiplication rounds a
 * subnormal result. */
#define RT_COMPLEX_DIV(T, NAME, TAG, MAXEXP, MANT, ABS, SIGN, INF) \
static T TAG##_pow2(int e) \
{ \
	T p = 1, b = e < 0 ? (T)0.5 : (T)2; \
	unsigned n = e < 0 ? -e : e; \
	for (; n; n >>= 1) { if (n & 1) p *= b; if (n > 1) b *= b; } \
	return p; \
} \
static T TAG##_scale(T x, int e) \
{ \
	const int hi = MAXEXP - 1, min = 2 - MAXEXP, lo = min + MANT; \
	if (e > hi) { x *= TAG##_pow2(hi); e -= hi; \
		if (e > hi) { x *= TAG##_pow2(hi); e -= hi; if (e > hi) e = hi; } } \
	if (e < min) { x *= TAG##_pow2(lo); e -= lo; \
		if (e < min) { x *= TAG##_pow2(lo); e -= lo; if (e < min) e = min; } } \
	return x * TAG##_pow2(e); \
} \
_Complex T NAME(T a, T b, T c, T d) \
{ \
	T m = ABS(c), n = ABS(d); \
	if (m != m || n > m) m = n; \
	int e = 0; \
	if (m > 0 && __builtin_isfinite(m)) { \
		while (m >= (T)0x1p64) { m *= (T)0x1p-64; e += 64; } \
		while (m < (T)0x1p-64) { m *= (T)0x1p64; e -= 64; } \
		while (m >= 2) { m *= (T)0.5; e++; } \
		while (m < 1) { m *= 2; e--; } \
		c = TAG##_scale(c, -e); d = TAG##_scale(d, -e); \
	} \
	T den = c * c + d * d; \
	T re = TAG##_scale((a * c + b * d) / den, -e); \
	T im = TAG##_scale((b * c - a * d) / den, -e); \
	if (__builtin_isnan(re) && __builtin_isnan(im)) { \
		if (den == 0 && (!__builtin_isnan(a) || !__builtin_isnan(b))) { \
			re = SIGN(INF, c) * a; im = SIGN(INF, c) * b; \
		} else if ((__builtin_isinf(a) || __builtin_isinf(b)) && \
		           __builtin_isfinite(c) && __builtin_isfinite(d)) { \
			a = SIGN((T)!!__builtin_isinf(a), a); \
			b = SIGN((T)!!__builtin_isinf(b), b); \
			re = INF * (a * c + b * d); im = INF * (b * c - a * d); \
		} else if (__builtin_isinf(m) && __builtin_isfinite(a) && __builtin_isfinite(b)) { \
			c = SIGN((T)!!__builtin_isinf(c), c); \
			d = SIGN((T)!!__builtin_isinf(d), d); \
			re = (T)0 * (a * c + b * d); im = (T)0 * (b * c - a * d); \
		} \
	} \
	_Complex T r; __real__ r = re; __imag__ r = im; return r; \
}
RT_COMPLEX_DIV(float, __divsc3, rt_cdivf, 128, 24,
               __builtin_fabsf, __builtin_copysignf, __builtin_inff())
RT_COMPLEX_DIV(double, __divdc3, rt_cdiv, 1024, 53,
               __builtin_fabs, __builtin_copysign, __builtin_inf())
#if __LDBL_MANT_DIG__ == 113
RT_COMPLEX_DIV(long double, __divtc3, rt_cdivl, 16384, 113,
               __builtin_fabsl, __builtin_copysignl, __builtin_infl())
#elif __LDBL_MANT_DIG__ == 64
RT_COMPLEX_DIV(long double, __divxc3, rt_cdivl, 16384, 64,
               __builtin_fabsl, __builtin_copysignl, __builtin_infl())
#endif
#undef RT_COMPLEX_DIV
/* Work on integer IEEE encodings: casts here would call these same helpers.
 * Round to nearest, ties to even, directly from the full 128-bit integer. */
static uint64_t rt_ti_ieee(unsigned __int128 m, int sign, int p, int bias)
{
	if (!m) return 0;
	int e = 0;
	for (unsigned __int128 n = m; n >>= 1;) e++;
	int shift = e - p;
	uint64_t q;
	if (shift > 0) {
		q = m >> shift;
		unsigned __int128 rem = m & (((unsigned __int128)1 << shift) - 1);
		unsigned __int128 half = (unsigned __int128)1 << (shift - 1);
		q += rem > half || (rem == half && (q & 1));
	} else q = (uint64_t)m << -shift;
	return ((uint64_t)sign << (p + (bias == 127 ? 8 : 11))) +
	       ((uint64_t)(e + bias - 1) << p) + q;
}
static unsigned __int128 rt_ieee_ti(uint64_t bits, int p, int bias)
{
	int e = (int)((bits >> p) & (bias * 2 + 1)) - bias;
	if (e < 0) return 0;
	if (e >= 128) return ~(unsigned __int128)0;
	unsigned __int128 m = (bits & ((UINT64_C(1) << p) - 1)) | (UINT64_C(1) << p);
	return e >= p ? m << (e - p) : m >> (p - e);
}
float __floattisf(__int128 a)
{
	union { float f; uint32_t u; } r = { .u = rt_ti_ieee(a < 0 ? -(unsigned __int128)a : (unsigned __int128)a, a < 0, 23, 127) };
	return r.f;
}
__int128 __fixsfti(float a)
{
	union { float f; uint32_t u; } r = { .f = a };
	unsigned __int128 m = rt_ieee_ti(r.u, 23, 127);
	return (__int128)(a < 0 ? -m : m);
}

float __floatuntisf(unsigned __int128 a)
{
	union { float f; uint32_t u; } r = { .u = rt_ti_ieee(a, 0, 23, 127) };
	return r.f;
}
unsigned __int128 __fixunssfti(float a)
{
	union { float f; uint32_t u; } r = { .f = a };
	unsigned __int128 m = rt_ieee_ti(r.u, 23, 127);
	return a < 0 ? 0 : m;
}

double __floattidf(__int128 a)
{
	union { double f; uint64_t u; } r = { .u = rt_ti_ieee(a < 0 ? -(unsigned __int128)a : (unsigned __int128)a, a < 0, 52, 1023) };
	return r.f;
}
__int128 __fixdfti(double a)
{
	union { double f; uint64_t u; } r = { .f = a };
	unsigned __int128 m = rt_ieee_ti(r.u, 52, 1023);
	return (__int128)(a < 0 ? -m : m);
}

double __floatuntidf(unsigned __int128 a)
{
	union { double f; uint64_t u; } r = { .u = rt_ti_ieee(a, 0, 52, 1023) };
	return r.f;
}
unsigned __int128 __fixunsdfti(double a)
{
	union { double f; uint64_t u; } r = { .f = a };
	unsigned __int128 m = rt_ieee_ti(r.u, 52, 1023);
	return a < 0 ? 0 : m;
}

/* calls clang leaves for GNU builtins it lacks */
int puts(const char *);
void __builtin_exit(int c) __attribute__((noreturn));
void __builtin_exit(int c) { exit(c); __builtin_unreachable(); }
int __builtin_puts(const char *p) { return puts(p); }
int __builtin_isinff(float x) { return __builtin_isinf(x); }
void atomic_signal_fence(int order) { (void)order; __asm__ volatile("" ::: "memory"); }

/* ---- qsort (insertion sort; the tests sort a handful of items) ---- */
void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
	char *a = b, t[256];
	if (sz > sizeof t) return;
	for (size_t i = 1; i < n; i++)
		for (size_t j = i; j && cmp(a + (j - 1) * sz, a + j * sz) > 0; j--) {
			memcpy(t, a + j * sz, sz);
			memcpy(a + j * sz, a + (j - 1) * sz, sz);
			memcpy(a + (j - 1) * sz, t, sz);
		}
}
void *bsearch(const void *k, const void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
	for (size_t i = 0; i < n; i++)
		if (!cmp(k, (const char *)b + i * sz)) return (char *)b + i * sz;
	return 0;
}

/* ---- printf ---- */
typedef struct { char *p; size_t n, cap; int fd; } sink;
static void put(sink *s, int c)
{
	if (s->fd) outc(c);
	else if (s->n + 1 < s->cap) s->p[s->n] = (char)c;
	s->n++;
}
static void puts_n(sink *s, const char *p, size_t n) { while (n--) put(s, *p++); }
static void pad(sink *s, int c, int n) { while (n-- > 0) put(s, c); }

/* emit sign/prefix, zero padding and digits within width */
static void field(sink *s, const char *pre, const char *dig, int nd, int prec, int width, int left, int zero)
{
	int np = (int)strlen(pre), z = prec > nd ? prec - nd : 0;
	int len = np + z + nd;
	if (!left && !zero) pad(s, ' ', width - len);
	puts_n(s, pre, (size_t)np);
	if (!left && zero) pad(s, '0', width - len);
	pad(s, '0', z);
	puts_n(s, dig, (size_t)nd);
	if (left) pad(s, ' ', width - len);
}

/* decimal digits of a non-negative double into b: integer part, '.', prec fraction digits */
static int fixed(char *b, double v, int prec, int alt)
{
	char ib[400];
	int ni = 0, n = 0;
	double r = 0.5;
	for (int i = 0; i < prec; i++) r /= 10;
	v += r;
	double ip = __builtin_floor(v), fp = v - ip;
	if (ip < 1) ib[ni++] = '0';
	while (ip >= 1 && ni < 390) {
		double q = __builtin_floor(ip / 10);
		ib[ni++] = (char)('0' + (int)(ip - q * 10));
		ip = q;
	}
	while (ni) b[n++] = ib[--ni];
	if (prec || alt) b[n++] = '.';
	for (int i = 0; i < prec && n < 500; i++) {
		fp *= 10;
		int d = (int)fp;
		if (d > 9) d = 9;
		b[n++] = (char)('0' + d);
		fp -= d;
	}
	return n;
}
static int expo(char *b, double v, int prec, int alt, int up)
{
	int e = 0, n;
	if (v != 0) {
		while (v >= 10) v /= 10, e++;
		while (v < 1) v *= 10, e--;
		double r = 0.5;
		for (int i = 0; i < prec; i++) r /= 10;
		if (v + r >= 10) v /= 10, e++;
	}
	n = fixed(b, v, prec, alt);
	b[n++] = up ? 'E' : 'e';
	b[n++] = e < 0 ? '-' : '+';
	if (e < 0) e = -e;
	if (e >= 100) b[n++] = (char)('0' + e / 100);
	b[n++] = (char)('0' + e / 10 % 10);
	b[n++] = (char)('0' + e % 10);
	return n;
}

static int fmt(sink *s, const char *f, va_list ap)
{
	char b[520];
	for (; *f; f++) {
		int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1, lng = 0;
		if (*f != '%') { put(s, *f); continue; }
		for (f++;; f++) {
			if (*f == '-') left = 1;
			else if (*f == '+') plus = 1;
			else if (*f == ' ') space = 1;
			else if (*f == '#') alt = 1;
			else if (*f == '0') zero = 1;
			else break;
		}
		if (*f == '*') { width = va_arg(ap, int); if (width < 0) left = 1, width = -width; f++; }
		else while (isdigit(*f)) width = width * 10 + *f++ - '0';
		if (*f == '.') {
			f++; prec = 0;
			if (*f == '*') { prec = va_arg(ap, int); f++; }
			else while (isdigit(*f)) prec = prec * 10 + *f++ - '0';
		}
		for (;; f++) {
			if (*f == 'h') lng--;
			else if (*f == 'l' || *f == 'j' || *f == 'z' || *f == 't' || *f == 'q') lng++;
			else if (*f == 'L') lng = 3;
			else break;
		}
		if (left) zero = 0;
		switch (*f) {
		case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': case 'b': {
			unsigned long long v;
			int sgn = *f == 'd' || *f == 'i', base = *f == 'o' ? 8 : *f == 'b' ? 2 : (*f == 'x' || *f == 'X' || *f == 'p') ? 16 : 10;
			const char *dg = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
			char pre[4] = { 0 };
			int nd = 0;
			char tmp[70];
			if (*f == 'p') { v = (uintptr_t)va_arg(ap, void *); alt = 1; }
			else if (lng >= 2) v = va_arg(ap, unsigned long long);
			else if (lng == 1) v = sgn ? (unsigned long long)va_arg(ap, long) : va_arg(ap, unsigned long);
			else {
				v = sgn ? (unsigned long long)(long long)va_arg(ap, int) : va_arg(ap, unsigned int);
				if (lng == -1) v = sgn ? (unsigned long long)(long long)(short)v : (unsigned short)v;
				if (lng <= -2) v = sgn ? (unsigned long long)(long long)(signed char)v : (unsigned char)v;
			}
			if (sgn && (long long)v < 0) { pre[0] = '-'; v = -v; }
			else if (sgn && plus) pre[0] = '+';
			else if (sgn && space) pre[0] = ' ';
			while (v) { tmp[nd++] = dg[v % base]; v /= base; }
			if (alt && base == 16 && (nd || *f == 'p')) { pre[0] = '0'; pre[1] = *f == 'X' ? 'X' : 'x'; }
			if (alt && base == 8 && prec <= nd) prec = nd + 1;
			if (!nd && prec) tmp[nd++] = '0';
			for (int i = 0; i < nd; i++) b[i] = tmp[nd - 1 - i];
			if (prec >= 0) zero = 0;
			field(s, pre, b, nd, prec, width, left, zero);
			break;
		}
		case 'c':
			b[0] = (char)va_arg(ap, int);
			field(s, "", b, 1, -1, width, left, 0);
			break;
		case 's': {
			const char *p = va_arg(ap, const char *);
			if (!p) p = "(null)";
			int n = (int)(prec >= 0 ? strnlen(p, (size_t)prec) : strlen(p));
			field(s, "", p, n, -1, width, left, 0);
			break;
		}
		case 'n':
			*va_arg(ap, int *) = (int)s->n;
			break;
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
			double v = lng == 3 ? ld2d(va_arg(ap, long double)) : va_arg(ap, double);
			char pre[2] = { 0 };
			int n, up = *f == 'E' || *f == 'G' || *f == 'F';
			if (__builtin_signbit(v)) { pre[0] = '-'; v = -v; }
			else if (plus) pre[0] = '+';
			else if (space) pre[0] = ' ';
			if (prec < 0) prec = 6;
			if (v != v || v == __builtin_inf()) {
				const char *t = v != v ? (up ? "NAN" : "nan") : (up ? "INF" : "inf");
				field(s, pre, t, 3, -1, width, left, 0);
				break;
			}
			if (*f == 'f' || *f == 'F') n = fixed(b, v, prec, alt);
			else if (*f == 'e' || *f == 'E' || *f == 'a' || *f == 'A') n = expo(b, v, prec, alt, up);
			else {
				int p = prec ? prec : 1, x = 0;
				char t[520];
				t[expo(t, v, p - 1, 0, 0)] = 0;	/* strtol below: no stack bytes after the exponent */
				x = (int)strtol(strchr(t, 'e') + 1, 0, 10);
				if (x < -4 || x >= p) n = expo(b, v, p - 1, alt, up);
				else n = fixed(b, v, p - 1 - x, alt);
				if (!alt && memchr(b, '.', (size_t)n)) {
					int e = n;
					char *ep = memchr(b, up ? 'E' : 'e', (size_t)n);
					if (ep) e = (int)(ep - b);
					int k = e;
					while (b[k - 1] == '0') k--;
					if (b[k - 1] == '.') k--;
					memmove(b + k, b + e, (size_t)(n - e));
					n -= e - k;
				}
			}
			field(s, pre, b, n, -1, width, left, zero);
			break;
		}
		case '%': put(s, '%'); break;
		default: put(s, '%'); put(s, *f); break;
		}
	}
	return (int)s->n;
}

int vsnprintf(char *p, size_t n, const char *f, va_list ap)
{
	sink s = { p, 0, n, 0 };
	int r = fmt(&s, f, ap);
	if (n) p[s.n < n ? s.n : n - 1] = 0;
	return r;
}
int vsprintf(char *p, const char *f, va_list ap) { return vsnprintf(p, (size_t)-1 >> 1, f, ap); }
int vfprintf(FILE *fp, const char *f, va_list ap) { sink s = { 0, 0, 0, fp->fd ? fp->fd : 1 }; return fmt(&s, f, ap); }
int vprintf(const char *f, va_list ap) { return vfprintf(stdout, f, ap); }
int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vprintf(f, ap); va_end(ap); return r; }
int fprintf(FILE *fp, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(fp, f, ap); va_end(ap); return r; }
int sprintf(char *p, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsprintf(p, f, ap); va_end(ap); return r; }
int snprintf(char *p, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(p, n, f, ap); va_end(ap); return r; }
int putchar(int c) { outc(c); return (unsigned char)c; }
int fputc(int c, FILE *f) { (void)f; return putchar(c); }
int putc(int c, FILE *f) { return fputc(c, f); }
int fputs(const char *p, FILE *f) { (void)f; while (*p) outc(*p++); return 0; }
int puts(const char *p) { fputs(p, stdout); outc('\n'); return 0; }
size_t fwrite(const void *p, size_t a, size_t b, FILE *f) { (void)f; const char *c = p; for (size_t i = 0; i < a * b; i++) outc(c[i]); return b; }
long write(int fd, const void *p, size_t n) { (void)fd; fwrite(p, 1, n, stdout); return (long)n; }
int fflush(FILE *f) { (void)f; return 0; }
int getchar(void) { return -1; }
int fgetc(FILE *f) { (void)f; return -1; }
int getc(FILE *f) { (void)f; return -1; }
char *fgets(char *p, int n, FILE *f) { (void)p; (void)n; (void)f; return 0; }
size_t fread(void *p, size_t a, size_t b, FILE *f) { (void)p; (void)a; (void)b; (void)f; return 0; }
FILE *fopen(const char *p, const char *m) { (void)p; (void)m; return 0; }
int fclose(FILE *f) { (void)f; return 0; }
int feof(FILE *f) { (void)f; return 1; }
int remove(const char *p) { (void)p; return -1; }
char *tmpnam(char *s) { static char b[20] = "/tmp/rt-tmpnam"; if (s) { memcpy(s, b, sizeof b); return s; } return b; }
void perror(const char *p) { fprintf(stderr, "%s: error\n", p); }

/* main may take argc/argv */
extern int main(int, char **, char **);
int __rt_main(void)
{
	static char *argv[] = { "a.out", 0 }, *envp[] = { 0 };
	extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
	extern void (*__fini_array_start[])(void), (*__fini_array_end[])(void);
	for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();
	int r = main(1, argv, envp);
	for (void (**f)(void) = __fini_array_end; f > __fini_array_start;) (*--f)();
	exit(r);
	return r;
}

#ifndef __x86_64__	/* (x86-64: libgcc's, for __float128) */
/*
 * ---- binary128 soft float (compiler-rt's names and ABI: q registers) ----
 * Only integer arithmetic here, so clang does not call these from within.
 */
typedef unsigned __int128 tf_u;
typedef union { long double f; tf_u u; } tf_v;
#define TF_M112 (((tf_u)1 << 112) - 1)

static long double tf_mk(tf_u u) { tf_v v; v.u = u; return v.f; }
static tf_u tf_bits(long double f) { tf_v v; v.f = f; return v.u; }
static int tf_nan(tf_u u) { return (u << 1) > ((tf_u)0x7fff << 113); }
static long double tf_qnan(void) { return tf_mk((tf_u)0x7fff8 << 108); }

/* sign, biased exponent, significand m whose leading bit belongs at 3 + p; rounds to nearest even */
static tf_u tf_round(int s, int e, tf_u m, int p, int emax)
{
	unsigned g;

	if (!m)
		return (tf_u)s << (p + (emax == 0x7fff ? 15 : emax == 0x7ff ? 11 : 8));
	while (m >> (p + 4)) {
		m = (m >> 1) | (m & 1);
		e++;
	}
	while (!(m >> (p + 3))) {
		m <<= 1;
		e--;
	}
	if (e <= 0) {
		int sh = 1 - e;

		m = sh > p + 4 ? 1 : (m >> sh) | ((m & (((tf_u)1 << sh) - 1)) != 0);
		e = 0;
	}
	g = (unsigned)(m & 7);
	m >>= 3;
	if (g > 4 || (g == 4 && (m & 1)))
		m++;
	if (e && (m >> (p + 1))) {
		m >>= 1;
		e++;
	}
	if (e >= emax)
		m = 0, e = emax;
	else if (e)
		m &= ((tf_u)1 << p) - 1;
	{
		int eb = emax == 0x7fff ? 15 : emax == 0x7ff ? 11 : 8;

		return (tf_u)s << (p + eb) | (tf_u)e << p | m;
	}
}

/* exponent (subnormals: 1) and significand with the implicit bit at 112 */
static void tf_un(tf_u u, int *s, int *e, tf_u *m)
{
	*s = (int)(u >> 127);
	*e = (int)(u >> 112) & 0x7fff;
	*m = u & TF_M112;
	if (*e)
		*m |= (tf_u)1 << 112;
	else
		*e = 1;
}

static int tf_inf(tf_u u) { return (u << 1) == ((tf_u)0x7fff << 113); }

static long double tf_add(tf_u a, tf_u b)
{
	int sa, ea, sb, eb, d;
	tf_u ma, mb, m;

	if (tf_nan(a) || tf_nan(b))
		return tf_qnan();
	if (tf_inf(a))
		return tf_inf(b) && (a ^ b) >> 127 ? tf_qnan() : tf_mk(a);
	if (tf_inf(b))
		return tf_mk(b);
	tf_un(a, &sa, &ea, &ma);
	tf_un(b, &sb, &eb, &mb);
	if (!ma && !mb)
		return tf_mk((tf_u)(sa & sb) << 127);
	ma <<= 3, mb <<= 3;
	if (ea < eb || (ea == eb && ma < mb)) {
		tf_u t = ma; int ti = ea;

		ma = mb, mb = t, ea = eb, eb = ti;
		ti = sa, sa = sb, sb = ti;
	}
	d = ea - eb;
	if (d > 120)
		mb = mb != 0;
	else if (d)
		mb = (mb >> d) | ((mb & (((tf_u)1 << d) - 1)) != 0);
	m = sa == sb ? ma + mb : ma - mb;
	if (!m)
		return tf_mk(0);
	return tf_mk(tf_round(sa, ea, m, 112, 0x7fff));
}

long double __addtf3(long double a, long double b) { return tf_add(tf_bits(a), tf_bits(b)); }
long double __subtf3(long double a, long double b) { return tf_add(tf_bits(a), tf_bits(b) ^ ((tf_u)1 << 127)); }
long double __negtf2(long double a) { return tf_mk(tf_bits(a) ^ ((tf_u)1 << 127)); }

static void tf_norm(int *e, tf_u *m)
{
	while (*m && !(*m >> 112)) {
		*m <<= 1;
		--*e;
	}
}

long double __multf3(long double fa, long double fb)
{
	tf_u a = tf_bits(fa), b = tf_bits(fb), ma, mb, p00, p01, p10, p11, mid, lo, hi;
	int sa, ea, sb, eb, s;
	unsigned long long a0, a1, b0, b1;

	s = (int)((a ^ b) >> 127);
	if (tf_nan(a) || tf_nan(b))
		return tf_qnan();
	tf_un(a, &sa, &ea, &ma);
	tf_un(b, &sb, &eb, &mb);
	if (tf_inf(a) || tf_inf(b))
		return (!ma || !mb) ? tf_qnan() : tf_mk((tf_u)s << 127 | (tf_u)0x7fff << 112);
	if (!ma || !mb)
		return tf_mk((tf_u)s << 127);
	tf_norm(&ea, &ma);
	tf_norm(&eb, &mb);
	a0 = (unsigned long long)ma, a1 = (unsigned long long)(ma >> 64);
	b0 = (unsigned long long)mb, b1 = (unsigned long long)(mb >> 64);
	p00 = (tf_u)a0 * b0, p01 = (tf_u)a0 * b1, p10 = (tf_u)a1 * b0, p11 = (tf_u)a1 * b1;
	mid = p01 + p10;
	lo = p00 + (mid << 64);
	hi = p11 + (mid >> 64) + (lo < p00);
	/* product: leading bit at 224 or 225; to 115 */
	lo = (hi << 19) | (lo >> 109) | ((lo & (((tf_u)1 << 109) - 1)) != 0);
	return tf_mk(tf_round(s, ea + eb - 16383, lo, 112, 0x7fff));
}

long double __divtf3(long double fa, long double fb)
{
	tf_u a = tf_bits(fa), b = tf_bits(fb), ma, mb, q = 0;
	int sa, ea, sb, eb, s, e;

	s = (int)((a ^ b) >> 127);
	if (tf_nan(a) || tf_nan(b))
		return tf_qnan();
	tf_un(a, &sa, &ea, &ma);
	tf_un(b, &sb, &eb, &mb);
	if (tf_inf(a))
		return tf_inf(b) ? tf_qnan() : tf_mk((tf_u)s << 127 | (tf_u)0x7fff << 112);
	if (tf_inf(b))
		return tf_mk((tf_u)s << 127);
	if (!mb)
		return !ma ? tf_qnan() : tf_mk((tf_u)s << 127 | (tf_u)0x7fff << 112);
	if (!ma)
		return tf_mk((tf_u)s << 127);
	tf_norm(&ea, &ma);
	tf_norm(&eb, &mb);
	e = ea - eb + 16383;
	if (ma < mb) {
		ma <<= 1;
		e--;
	}
	for (int i = 0; i < 116; i++) {
		q <<= 1;
		if (ma >= mb) {
			ma -= mb;
			q |= 1;
		}
		ma <<= 1;
	}
	return tf_mk(tf_round(s, e, q | (ma != 0), 112, 0x7fff));
}

/* -1, 0, 1; unordered: u */
static int tf_cmp(long double fa, long double fb, int u)
{
	tf_u a = tf_bits(fa), b = tf_bits(fb);

	if (tf_nan(a) || tf_nan(b))
		return u;
	if (!((a | b) << 1))
		return 0;
	if ((a >> 127) != (b >> 127))
		return a >> 127 ? -1 : 1;
	if (a == b)
		return 0;
	return ((a < b) ^ (int)(a >> 127)) ? -1 : 1;
}
int __eqtf2(long double a, long double b) { return tf_cmp(a, b, 1); }
int __netf2(long double a, long double b) { return tf_cmp(a, b, 1); }
int __lttf2(long double a, long double b) { return tf_cmp(a, b, 1); }
int __letf2(long double a, long double b) { return tf_cmp(a, b, 1); }
int __cmptf2(long double a, long double b) { return tf_cmp(a, b, 1); }
int __gttf2(long double a, long double b) { return tf_cmp(a, b, -1); }
int __getf2(long double a, long double b) { return tf_cmp(a, b, -1); }
int __unordtf2(long double a, long double b) { return tf_nan(tf_bits(a)) || tf_nan(tf_bits(b)); }

long double __extenddftf2(double d)
{
	union { double d; unsigned long long u; } v = { d };
	int s = (int)(v.u >> 63), e = (int)(v.u >> 52) & 0x7ff;
	tf_u m = v.u & ((1ull << 52) - 1);

	if (e == 0x7ff)
		return tf_mk((tf_u)s << 127 | (tf_u)0x7fff << 112 | m << 60 | (m ? (tf_u)1 << 111 : 0));
	if (!e) {
		if (!m)
			return tf_mk((tf_u)s << 127);
		e = 1;
		while (!(m >> 52)) {
			m <<= 1;
			e--;
		}
		m &= (1ull << 52) - 1;
	}
	return tf_mk((tf_u)s << 127 | (tf_u)(e - 1023 + 16383) << 112 | m << 60);
}
long double __extendsftf2(float f) { return __extenddftf2((double)f); }

static tf_u tf_trunc(long double f, int p, int emax, int bias)
{
	tf_u a = tf_bits(f), m;
	int s, e, eb = emax == 0x7ff ? 11 : 8;

	tf_un(a, &s, &e, &m);
	if (tf_nan(a))
		return (tf_u)emax << p | (tf_u)1 << (p - 1);
	if (tf_inf(a))
		return (tf_u)s << (p + eb) | (tf_u)emax << p;
	if (!m)
		return (tf_u)s << (p + eb);
	tf_norm(&e, &m);
	/* leading bit to p + 3: shift right 109 - p with sticky */
	m = (m >> (109 - p)) | ((m & (((tf_u)1 << (109 - p)) - 1)) != 0);
	return tf_round(s, e - 16383 + bias, m, p, emax);
}
double __trunctfdf2(long double f)
{
	union { unsigned long long u; double d; } r = { (unsigned long long)tf_trunc(f, 52, 0x7ff, 1023) };
	return r.d;
}
float __trunctfsf2(long double f)
{
	union { unsigned u; float f; } r = { (unsigned)tf_trunc(f, 23, 0xff, 127) };
	return r.f;
}

static long double tf_fromu(int s, tf_u x) { return tf_mk(tf_round(s, 16383 + 115, x, 112, 0x7fff)); }
long double __floatditf(long long i) { return tf_fromu(i < 0, i < 0 ? -(tf_u)i : (tf_u)i); }
long double __floatsitf(int i) { return __floatditf(i); }
long double __floatunditf(unsigned long long i) { return tf_fromu(0, i); }
long double __floatunsitf(unsigned i) { return tf_fromu(0, i); }
long double __floattitf(__int128 i) { return i < 0 ? tf_mk(tf_round(1, 16383 + 115, -(tf_u)i, 112, 0x7fff)) : tf_fromu(0, (tf_u)i); }
long double __floatuntitf(tf_u i) { return tf_fromu(0, i); }

/* truncated magnitude (saturating at 2^128) */
static tf_u tf_toint(long double f, int *s)
{
	tf_u m;
	int e;

	tf_un(tf_bits(f), s, &e, &m);
	if (tf_nan(tf_bits(f)) || e < 16383)
		return 0;
	e -= 16383;
	if (e >= 128)
		return ~(tf_u)0;
	return e >= 112 ? m << (e - 112) : m >> (112 - e);
}
long long __fixtfdi(long double f) { int s; tf_u m = tf_toint(f, &s); return s ? -(long long)m : (long long)m; }
int __fixtfsi(long double f) { return (int)__fixtfdi(f); }
unsigned long long __fixunstfdi(long double f) { int s; tf_u m = tf_toint(f, &s); return s ? 0 : (unsigned long long)m; }
unsigned __fixunstfsi(long double f) { return (unsigned)__fixunstfdi(f); }
__int128 __fixtfti(long double f) { int s; tf_u m = tf_toint(f, &s); return s ? -(__int128)m : (__int128)m; }
tf_u __fixunstfti(long double f) { int s; tf_u m = tf_toint(f, &s); return s ? 0 : m; }
#endif

/* Scalar bfloat conversions for freestanding ARM tests. Production Linux links
 * the same ABI entry points from libgcc. Work on integer IEEE encodings so the
 * compiler building this runtime cannot emit calls back to these helpers. */
static uint16_t rt_bfloat_bits(uint64_t u, unsigned p, unsigned eb, int bias)
{
    uint16_t sign = (uint16_t)((u >> (p + eb)) << 15);
    uint64_t mask = (UINT64_C(1) << p) - 1, m = u & mask;
    unsigned ex = (unsigned)(u >> p) & ((1u << eb) - 1);
    int e, shift;
    uint64_t q, rem, halfway;
    if (ex == (1u << eb) - 1)
        return sign | 0x7f80 | (m ? 0x40 | (unsigned)(m >> (p - 7)) : 0);
    if (!ex && !m) return sign;
    e = (ex ? (int)ex : 1) - bias;
    if (ex) m |= UINT64_C(1) << p;
    if (e > 127) return sign | 0x7f80;
    shift = (int)p - 7 + (e < -126 ? -126 - e : 0);
    if (shift >= 64) return sign;
    q = m >> shift;
    rem = m & ((UINT64_C(1) << shift) - 1);
    halfway = UINT64_C(1) << (shift - 1);
    q += rem > halfway || (rem == halfway && (q & 1));
    return sign | (uint16_t)((e >= -126 ? (e + 126) << 7 : 0) + q);
}
float __extendbfsf2(__bf16 x)
{
    union { __bf16 f; uint16_t u; } a = { .f = x };
    union { float f; uint32_t u; } b = { .u = (uint32_t)a.u << 16 };
    return b.f;
}
__bf16 __truncsfbf2(float x)
{
    union { float f; uint32_t u; } a = { .f = x };
    union { __bf16 f; uint16_t u; } b = { .u = rt_bfloat_bits(a.u, 23, 8, 127) };
    return b.f;
}
__bf16 __truncdfbf2(double x)
{
    union { double f; uint64_t u; } a = { .f = x };
    union { __bf16 f; uint16_t u; } b = { .u = rt_bfloat_bits(a.u, 52, 11, 1023) };
    return b.f;
}
