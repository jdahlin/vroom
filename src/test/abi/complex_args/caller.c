#include <stdio.h>
#include "cx.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
	volatile double v = 2.0;
	_Complex float f = cf(1 + 2.0if, 3 + 4.0if, (float)v);
	_Complex double d = cd(v + 3.0i, 1, 2.0 + 3.0i);
	_Complex long double l = cld(1.5L, 2.0L + 3.0iL, 4.0f + 5.0if, 6.0 + 7.0i);
	_Complex int i = ci(1 + 2i, (_Complex short)(3 + 4i));
	struct rc r = crec((struct rc){ 1.5f + 2.5if, v }, 1 + 4.0i);

	CHECK(__real__ f == -3 && __imag__ f == 10);
	CHECK(__real__ d == 1 && __imag__ d == 0);
	CHECK(__real__ l == 2.5L && __imag__ l == 12);
	CHECK(__real__ i == 4 && __imag__ i == 6);
	CHECK(__real__ r.f == 3 && __imag__ r.f == 5 && r.d == 6);
	CHECK(cvar(3, 1.0 + 1.0i, v + 2.0i, 3.0 + 0.5i) == 1 + 1 + 4 + 2 + 9 + 0.5);
	printf("%s\n", bad ? "FAIL" : "ok");
	return bad != 0;
}
