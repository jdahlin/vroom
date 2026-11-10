#include "agg.h"

struct s1 f_s1(struct s1 x) { x.c += 1; return x; }
struct s2 f_s2(struct s2 x) { x.s *= -3; return x; }
struct sf f_sf(struct sf x, float k) { x.f *= k; return x; }
struct sd f_sd(struct sd x) { x.d = x.d / 4 + 1; return x; }
struct nest f_nest(struct nest x) { x.a.in.f += 0.5f; return x; }
struct arr1 f_arr1(struct arr1 x) { x.a[0] ^= 0x5a5a; return x; }
struct s3 f_s3(struct s3 x) { struct s3 r = { x.c, x.b, x.a }; return r; }
struct s8 f_s8(struct s8 x) { struct s8 r = { x.b, x.a + x.b }; return r; }
struct s12 f_s12(struct s12 x) { x.a++; x.b *= 2; x.c--; return x; }
struct s16 f_s16(struct s16 x) { struct s16 r = { x.b, x.a - x.b }; return r; }
struct mix f_mix(struct mix x) { x.c ^= 1; x.d = -x.d; x.s += 100; return x; }
struct big f_big(struct big x, int k) { for (int i = 0; i < 6; i++) x.v[i] = x.v[i] * k + i; return x; }
struct bits f_bits(struct bits x) { x.a = ~x.a; x.b += 3; x.c >>= 1; return x; }
union un f_un(union un x) { x.c[5] = x.c[0] + 1; x.i += 1; return x; }
struct w128 f_w128(struct w128 x) { x.v = x.v * 3 - 1; return x; }
int f_e0(struct e0 a, int x, struct e0 b, int y) { (void)a, (void)b; return x * 100 + y; }
__int128 f_i128(__int128 a, int x, unsigned __int128 b) { return a * x + (__int128)(b >> 64); }
long double f_ld(long double a, double b, long double c) { return a * b - c; }
_Complex float f_cf(_Complex float a) { return a + a + 1.0if; }
_Complex double f_cd(_Complex double a, _Complex double b) { return a - b + 1; }
_Complex long double f_cld(_Complex long double a) { _Complex long double r; __real__ r = -__imag__ a; __imag__ r = __real__ a + 1; return r; }

long long f_many(int a, long long b, float c, double d, char e, short f, long double g, int h, struct s12 i, struct big j)
{
	return a + b * 2 + (long long)(c * 4) + (long long)(d * 8) + e * 16 + f * 32 + (long long)(g * 64) + h * 128 +
	       i.a + i.c + (long long)i.b + j.v[0] + j.v[5];
}

double f_vlist(int n, va_list ap)
{
	double s = 0;

	for (int i = 0; i < n; i++) {
		switch (va_arg(ap, int)) {
		case 'i': s += va_arg(ap, int); break;
		case 'l': s += (double)va_arg(ap, long long); break;
		case 'd': s += va_arg(ap, double); break;
		case 'L': s += (double)va_arg(ap, long double); break;
		case 'q': s += Q(va_arg(ap, __int128)); break;
		case 's': { struct s12 t = va_arg(ap, struct s12); s += t.a + t.b + t.c; break; }
		case 'b': { struct big t = va_arg(ap, struct big); for (int k = 0; k < 6; k++) s += (double)t.v[k]; break; }
		case 'p': s += *va_arg(ap, int *); break;
		default: return -1e9;
		}
	}
	return s;
}

double f_va(int n, ...)
{
	va_list ap;
	double r;

	va_start(ap, n);
	r = f_vlist(n, ap);
	va_end(ap);
	return r;
}

long long f_cb(long long (*fn)(struct s12, long double, struct s1), int k)
{
	struct s12 a = { k, 1.5f, -k };
	struct s1 c = { 'x' };

	return fn(a, 2.25L * k, c);
}

long long call_back_va(double (*fn)(int, ...))
{
	struct s12 s = { 1, 2, 3 };
	struct big b = { { 1, 2, 3, 4, 5, 6 } };
	int z = 7;

	return (long long)fn(6, 'i', -4, 'L', 1.5L, 's', s, 'b', b, 'q', (__int128)1 << 60, 'p', &z);
}
