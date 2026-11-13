#include "bitint.h"
#include <stdarg.h>
#define DEF(N) \
 S##N signed##N(S##N a) { return a; } \
 U##N unsigned##N(U##N a) { return a; } \
 S##N call##N(S##N a) { return callback##N(a); } \
 U##N stack##N(long a,long b,long c,long d,long e,U##N x,long f,long g,long h) { \
  if (a!=11 || b!=12 || c!=13 || d!=14 || e!=15 || f!=16 || g!=17 || h!=18) return 0; \
  return x; \
 }
DEF(2) DEF(7) DEF(9) DEF(17) DEF(31) DEF(32) DEF(33)
DEF(63) DEF(64) DEF(65) DEF(96) DEF(127) DEF(128)
unsigned _BitInt(1) one(unsigned _BitInt(1) a) { return a; }
U65 stackarm(long a,long b,long c,long d,long e,long f,long g,U65 x,long h) {
 if (a!=11 || b!=12 || c!=13 || d!=14 || e!=15 || f!=16 || g!=17 || h!=18) return 0;
 return x;
}
U128 variadic(int unused, ...) {
 va_list ap;
 va_start(ap, unused);
 U128 x = va_arg(ap, U128);
 U7 y = va_arg(ap, U7);
 U65 z = va_arg(ap, U65);
 U7 pad = va_arg(ap, U7);
 U128 w = va_arg(ap, U128);
 U128 v = va_arg(ap, U128);
 va_end(ap);
 return x ^ (U128)y ^ (U128)z ^ (U128)pad ^ w ^ v;
}
struct R record(struct R x) { x.b ^= (U65)1 << 64; x.c = -x.c; return x; }
