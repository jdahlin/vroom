// a class argument built from another type (F from 1, std::function from a lambda) in an operand that may
// not run (a && f(1), c ? f(3) : f(4)): a temporary destroyed only if built (json's parser(..., cb) in ?:)
#include <stdio.h>
int made, gone;
struct F { int k; F(int x) : k(x) { made++; } F(const F &o) : k(o.k) { made++; } ~F() { gone++; } };
static int take(F f, int x) { return f.k + x; }
int main(int argc, char **) {
	bool t = argc == 1;
	int r1, r2, r3;
	{
		r1 = t && take(1, 3) == 4; /* built, destroyed */
		r2 = !t && take(2, 3) == 5; /* never built */
		r3 = t ? take(3, 1) : take(4, 1);
	}
	printf("%d %d %d made %d gone %d\n", r1, r2, r3, made, gone);
	return r1 == 1 && r2 == 0 && r3 == 4 && made == gone && made >= 2 && made <= 4 ? 0 : 1;
}
