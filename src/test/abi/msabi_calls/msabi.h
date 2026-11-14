/* The convention that is not the target's own (PLAN-win64.md WIN2): ms_abi on x86-64 Linux,
   sysv_abi on Windows.  Each function and callback here uses it, so both of vcc's classifiers
   meet clang's on both OSes. */
#ifdef _WIN32
#define OTHER __attribute__((sysv_abi))
#else
#define OTHER __attribute__((ms_abi))
#endif

struct s1 { char c; };
struct s2 { short s; };
struct s3 { char c[3]; };
struct s4 { float f; };
struct s8 { int a, b; };
struct s12 { int a, b, c; };
struct s16 { double d; long long l; };
struct s24 { long long a, b, c; };
struct sd2 { double x, y; };
struct empty_pad { char c; double d; };

OTHER int ints(char a, short b, int c, long long d, unsigned char e, long long f, int g);
OTHER double fps(double a, float b, double c, float d, double e, float f);
OTHER double mixed(int a, double b, long long c, float d, int e, double f);
OTHER struct s1 r1(struct s1 a, struct s1 b);
OTHER struct s2 r2(struct s2 a);
OTHER struct s3 r3(struct s3 a, int k);
OTHER struct s4 r4(struct s4 a, double k);
OTHER struct s8 r8(struct s8 a, struct s8 b);
OTHER struct s12 r12(struct s12 a, int k);
OTHER struct s16 r16(struct s16 a, struct s16 b);
OTHER struct s24 r24(int k, struct s24 a);
OTHER struct sd2 rsd2(struct sd2 a);
OTHER struct empty_pad rpad(struct empty_pad a, struct empty_pad b, struct empty_pad c, struct empty_pad d, struct empty_pad e);
OTHER long double ld(long double a, int k, long double b);
OTHER __int128 i128(__int128 a, __int128 b, int k);
OTHER _Complex double cplx(_Complex double a, _Complex float b);
OTHER long long many(long long a, long long b, long long c, long long d, long long e, double f, long long g, double h, int i, struct s12 j);
OTHER int callback(OTHER int (*f)(int, double, struct s8), int x);
OTHER int keep(int x);	/* preserves what its convention's callee-saved registers hold */
#ifndef _WIN32
OTHER double ms_va(int n, ...);	/* __builtin_ms_va_list */
#endif
