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
static volatile unsigned int *const uart = (unsigned int *)0x09000000;
static void outc(int c) { *uart = (unsigned char)c; }

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
double sqrt(double x) { return __builtin_sqrt(x); }
float sqrtf(float x) { return __builtin_sqrtf(x); }
double floor(double x) { return __builtin_floor(x); }
double ceil(double x) { return __builtin_ceil(x); }
double trunc(double x) { return __builtin_trunc(x); }
double round(double x) { return __builtin_round(x); }
double copysign(double x, double y) { return __builtin_copysign(x, y); }
double fmod(double x, double y) { return x - __builtin_trunc(x / y) * y; }
double ldexp(double x, int e) { for (; e > 0; e--) x *= 2; for (; e < 0; e++) x /= 2; return x; }

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
				expo(t, v, p - 1, 0, 0);
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
