/* 16-byte alignment in argument passing: AAPCS64 rounds to an even register
   pair (and a 16-byte stack slot) by clang's unadjusted alignment (a
   record's fields, not its own aligned() or a typedef's); Apple never
   rounds the pair.  Registers, the stack and va_arg. */
#include <stdarg.h>

struct al { long long a, b; } __attribute__((aligned(16)));	/* aligned() only: not rounded */
struct ma { long long a __attribute__((aligned(16))); long long b; };	/* a field: rounded */
typedef struct { long long a, b; } plain;
typedef plain __attribute__((aligned(16))) tal;			/* a typedef: not rounded */
struct nest { struct al x; };					/* a field of an aligned record: rounded */
struct pk2 { __int128 v; } __attribute__((packed, aligned(16)));	/* packed: not rounded */
typedef __int128 i8a __attribute__((aligned(8)));
struct ia { i8a v; };						/* an 8-aligned field: not rounded */
struct w { __int128 v; };

long long r_al(int x, struct al y, int z);
long long r_ma(int x, struct ma y, int z);
long long r_tal(int x, tal y, int z);
long long r_nest(int x, struct nest y, int z);
long long r_pk2(int x, struct pk2 y, int z);
long long r_ia(int x, struct ia y, int z);
long long r_w(int x, struct w y, int z);
long long r_i128(int x, __int128 y, int z, unsigned __int128 u);
long long s_al(int a, int b, int c, int d, int e, int f, int g, int h, int i, struct al y, int j);
long long s_ma(int a, int b, int c, int d, int e, int f, int g, int h, int i, struct ma y, int j);
long long s_i128(int a, int b, int c, int d, int e, int f, int g, int h, int i, __int128 y, int j);
struct hv { double a, b, c, d; } __attribute__((aligned(32)));	/* an HFA, aligned() only */
double h_stack(double a, double b, double c, double d, double e, double f, double g, double h, double i, struct hv y, double j);
struct f3 { float a, b, c; };	/* Apple: 4-aligned and 12 bytes on the stack */
float f_stack(float a, float b, float c, float d, float e, float f, float g, float h, float i, struct f3 y, float j, struct f3 z);
double h_va(int n, ...);	/* n doubles, then a struct hv */
long long v_all(int n, ...);	/* 0: struct al, 1: struct ma, 2: __int128, 3: struct w, 4: int */
