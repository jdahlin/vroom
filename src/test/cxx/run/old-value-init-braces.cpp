// {} value-initializes a class without a user-provided default constructor: zeroed, as an argument
// (f({})), a local (T x{}) and a temporary (T{}); one with a user-provided constructor only runs it
#include <string.h>
struct P { int a, b; };
struct Q { int a; Q() : a(5) {} };
struct R { int a; P p; R() = default; };
static int h(int x, P p) { return x + p.a + p.b; }
static int k(Q q) { return q.a; }
static int m(R r) { return r.a + r.p.b; }
__attribute__((noinline)) static void junk() { volatile char b[512]; memset((char *)b, 0x55, sizeof b); }
int main() {
	junk(); int r1 = h(1, {});
	junk(); P q{}; int r2 = h(1, q);
	junk(); int r3 = h(1, P{});
	junk(); int r4 = k({});
	junk(); R s{}; int r5 = m(s) + m({});
	return r1 == 1 && r2 == 1 && r3 == 1 && r4 == 5 && r5 == 0 ? 0 : 1;
}
