// a function-local static of class type: constructed the first time its declaration runs, once, under
// the Itanium ABI's guard (__cxa_guard_acquire/release), its destructor registered with __cxa_atexit
#include <stdio.h>
int made, gone;
struct T { int v; T(int x) : v(x) { made++; } ~T() { gone++; printf("bye %d\n", v); } };
struct U { int v = 7; };
static int f(int k) { static T t(k); return t.v; }
static int g() { static U u; return u.v++; }
int main() { int r = f(3) + f(9) + g() + g(); printf("%d %d\n", r, made); return r == 3 + 3 + 7 + 8 && made == 1 ? 0 : 1; }
