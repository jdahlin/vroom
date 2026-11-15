// _Complex T: C's complex types in C++ (GNU): locals, parameters, results, arithmetic, __real__/__imag__,
// the imaginary suffix, comparisons, conversions, mangled C <T> (llvm-test-suite conditional-gnu-ext-cxx)
_Complex double add(_Complex double x, _Complex double y) { return x + y * x - y / x; }
double parts(_Complex double x) { return __real__ x + __imag__ x; }
_Complex int lit() { return 1 + 2i; }
_Complex double widen(_Complex float x) { return x; }
_Complex double scale(double d) { _Complex double c = d; c += 1.0; return c * 2.0; }
bool same(_Complex double a, _Complex double b) { return a == b && a != 0.0; }
static _Complex int pick(_Complex int a, _Complex int b) { return a ?: b; }
int main() {
	_Complex double x = 1.0 + 2.0i, y = 3.0;
	_Complex int l = lit(), z = 0;
	_Complex double s = add(x, y);
	return parts(x) == 3.0 && __real__ l == 1 && __imag__ l == 2 && __real__ widen(1.5f + 0.5if) == 1.5 &&
	       __real__ scale(2.0) == 6.0 && same(x, x) && !same(x, y) && __imag__ pick(z, l) == 2 &&
	       __real__ s == __real__ (x + y * x - y / x) ? 0 : 1;
}
