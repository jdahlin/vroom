#include "narrow.h"

signed char retc(int x) { return (signed char)x; }
unsigned char retuc(int x) { return (unsigned char)x; }
short adds(short a, short b) { return (short)(a + b); }
unsigned short retus(int x) { return (unsigned short)x; }
_Bool retb(int x) { return x & 2; }
enum small rete(int x) { return (enum small)x; }
signed char tailc(int x) { return retc(x + 1); }
int takes(signed char c, unsigned short u, _Bool b, short s) { return c * 1000000 + u * 10 + b + s * 100; }
long takes_many(long a, long b, long c, long d, long e, long f, long g, long h, signed char i, short j, unsigned char k)
{
	return a + b + c + d + e + f + g + h + i * 1000 + j * 10 + k;
}
int via(signed char (*f)(int), int x) { return f(x) + 1; }
int pass_on(int x) { return takes(x, x, x, x); }
