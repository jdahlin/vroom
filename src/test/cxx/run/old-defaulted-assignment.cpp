// T& operator=(T&&) = default and (const T&) = default of a class that is not trivially copyable: each
// member assigned in turn, a member's own operator= (copy or move, as the source is) called; and a = b
// with b an lvalue takes operator=(const T&), not (T&&)
#include <stdio.h>
int copies, moves;
struct R { int *p = nullptr; R &operator=(R &&o) { moves++; p = o.p; o.p = nullptr; return *this; } R &operator=(const R &o) { copies++; p = o.p; return *this; } };
struct M { int k = 0; R r; double d[2] = {0, 0}; M &operator=(M &&) = default; M &operator=(const M &) = default; };
int main() {
	int x = 5; M a, b; b.k = 3; b.r.p = &x; b.d[1] = 2.5;
	a = static_cast<M &&>(b);
	bool moved = a.k == 3 && *a.r.p == 5 && a.d[1] == 2.5 && b.r.p == nullptr && moves == 1 && copies == 0;
	M c; M &ref = (c = a);
	bool copied = c.k == 3 && *c.r.p == 5 && a.r.p == &x && copies == 1 && &ref == &c;
	R r1, r2; r1 = r2;
	printf("%d %d\n", moves, copies);
	return moved && copied && copies == 2 && moves == 1 ? 0 : 1;
}
