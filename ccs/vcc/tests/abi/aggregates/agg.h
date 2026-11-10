/* aggregates, wide scalars, complex and variadic calls across compilers
   (written for WebAssembly's C ABI, but target neutral) */
#include <stdarg.h>

struct e0 { };
struct s1 { char c; };
struct s2 { short s; };
struct sf { float f; };
struct sd { double d; };
struct nest { struct { struct sf in; } a; };	/* one element, nested: a float */
struct arr1 { int a[1]; };
struct s3 { char a, b, c; };
struct s8 { int a, b; };
struct s12 { int a; float b; int c; };
struct s16 { long long a, b; };
struct mix { char c; double d; short s; };
struct big { long long v[6]; };
struct bits { unsigned a : 3, b : 9, c : 20; };
union un { int i; float f; char c[6]; };
struct w128 { __int128 v; };
/* an __int128 vararg as a double, without a library conversion */
#define Q(x) ((double)(long long)((x) >> 8))

struct s1 f_s1(struct s1 x);
struct s2 f_s2(struct s2 x);
struct sf f_sf(struct sf x, float k);
struct sd f_sd(struct sd x);
struct nest f_nest(struct nest x);
struct arr1 f_arr1(struct arr1 x);
struct s3 f_s3(struct s3 x);
struct s8 f_s8(struct s8 x);
struct s12 f_s12(struct s12 x);
struct s16 f_s16(struct s16 x);
struct mix f_mix(struct mix x);
struct big f_big(struct big x, int k);
struct bits f_bits(struct bits x);
union un f_un(union un x);
struct w128 f_w128(struct w128 x);
int f_e0(struct e0 a, int x, struct e0 b, int y);
__int128 f_i128(__int128 a, int x, unsigned __int128 b);
long double f_ld(long double a, double b, long double c);
_Complex float f_cf(_Complex float a);
_Complex double f_cd(_Complex double a, _Complex double b);
_Complex long double f_cld(_Complex long double a);
long long f_many(int a, long long b, float c, double d, char e, short f, long double g, int h, struct s12 i, struct big j);
double f_va(int n, ...);	/* tags: 'i' int, 'l' long long, 'd' double, 'L' long double, 'q' __int128, 's' struct s12, 'b' struct big, 'p' pointer */
double f_vlist(int n, va_list ap);
long long f_cb(long long (*fn)(struct s12, long double, struct s1), int k);
long long caller_cb(struct s12 a, long double b, struct s1 c);
long long call_back_va(double (*fn)(int, ...));
