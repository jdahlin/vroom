#include <stdarg.h>
#include "cx.h"

/* (no complex * or /: the arm64 harness links no libgcc for __mulsc3 ...) */
_Complex float cf(_Complex float a, _Complex float b, float k)
{
	float re = __real__ a * __real__ b - __imag__ a * __imag__ b, im = __real__ a * __imag__ b + __imag__ a * __real__ b;

	return (re + k) + im * 1.0if;
}
_Complex double cd(_Complex double a, int i, _Complex double b) { return a - b + i; }
_Complex long double cld(long double a, _Complex long double z, _Complex float f, _Complex double d)
{
	_Complex long double r = z + z + a + __real__ f;

	__real__ r -= __imag__ d;
	__imag__ r += __real__ d;
	return r;
}
_Complex int ci(_Complex int a, _Complex short s) { return a + s; }
struct rc crec(struct rc r, _Complex double z) { struct rc o = { r.f + r.f, r.d + __imag__ z }; return o; }
double cvar(int n, ...)
{
	va_list ap;
	double s = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		_Complex double z = va_arg(ap, _Complex double);

		s += __real__ z * (i + 1) + __imag__ z;
	}
	va_end(ap);
	return s;
}
