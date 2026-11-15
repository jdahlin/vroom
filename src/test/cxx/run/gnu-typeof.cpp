// __typeof (expression) is its type, of an lvalue too; in a cast and a block's declaration (glibc's iscanonical)
#include <math.h>
int canon(double v) { return ((void) (__typeof (v)) (v), 1); }
int main() {
	double v = 2.5;
	__typeof(v) w = (__typeof__(v)) v;
	__typeof__(canon(v)) n = 1;
	return canon(v) + iscanonical(1.0) + n == 3 && w == 2.5 && sizeof(w) == sizeof(double) ? 0 : 1;
}
