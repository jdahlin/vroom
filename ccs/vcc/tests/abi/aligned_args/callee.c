#include "al.h"

long long r_al(int x, struct al y, int z) { return x + y.a * 3 + y.b * 5 + z * 7; }
long long r_ma(int x, struct ma y, int z) { return x + y.a * 3 + y.b * 5 + z * 7; }
long long r_tal(int x, tal y, int z) { return x + y.a * 3 + y.b * 5 + z * 7; }
long long r_nest(int x, struct nest y, int z) { return x + y.x.a * 3 + y.x.b * 5 + z * 7; }
long long r_pk2(int x, struct pk2 y, int z) { return x + (long long)y.v * 3 + (long long)(y.v >> 64) * 5 + z * 7; }
long long r_ia(int x, struct ia y, int z) { return x + (long long)y.v * 3 + (long long)(y.v >> 64) * 5 + z * 7; }
long long r_w(int x, struct w y, int z) { return x + (long long)y.v * 3 + (long long)(y.v >> 64) * 5 + z * 7; }
long long r_i128(int x, __int128 y, int z, unsigned __int128 u)
{
	return x + (long long)y * 3 + (long long)(y >> 64) * 5 + z * 7 + (long long)u * 11 + (long long)(u >> 64) * 13;
}
long long s_al(int a, int b, int c, int d, int e, int f, int g, int h, int i, struct al y, int j)
{
	return a + b + c + d + e + f + g + h + i * 2 + y.a * 3 + y.b * 5 + j * 7;
}
long long s_ma(int a, int b, int c, int d, int e, int f, int g, int h, int i, struct ma y, int j)
{
	return a + b + c + d + e + f + g + h + i * 2 + y.a * 3 + y.b * 5 + j * 7;
}
long long s_i128(int a, int b, int c, int d, int e, int f, int g, int h, int i, __int128 y, int j)
{
	return a + b + c + d + e + f + g + h + i * 2 + (long long)y * 3 + (long long)(y >> 64) * 5 + j * 7;
}
long long v_all(int n, ...)
{
	va_list ap;
	long long s = 0;

	va_start(ap, n);
	for (int k = 0; k < n; k++) {
		switch (va_arg(ap, int)) {
		case 0: { struct al y = va_arg(ap, struct al); s = s * 3 + y.a + y.b * 2; break; }
		case 1: { struct ma y = va_arg(ap, struct ma); s = s * 3 + y.a + y.b * 2; break; }
		case 2: { __int128 y = va_arg(ap, __int128); s = s * 3 + (long long)y + (long long)(y >> 64) * 2; break; }
		case 3: { struct w y = va_arg(ap, struct w); s = s * 3 + (long long)y.v + (long long)(y.v >> 64) * 2; break; }
		default: s = s * 3 + va_arg(ap, int); break;
		}
	}
	va_end(ap);
	return s;
}

double h_stack(double a, double b, double c, double d, double e, double f, double g, double h, double i, struct hv y, double j)
{
	return a + b + c + d + e + f + g + h + i * 2 + y.a * 3 + y.b * 5 + y.c * 7 + y.d * 11 + j * 13;
}

double h_va(int n, ...)
{
	va_list ap;
	double s = 0;
	struct hv y;

	va_start(ap, n);
	while (n--)
		s += va_arg(ap, double);
	y = va_arg(ap, struct hv);
	va_end(ap);
	return s + y.a * 3 + y.b * 5 + y.c * 7 + y.d * 11;
}

float f_stack(float a, float b, float c, float d, float e, float f, float g, float h, float i, struct f3 y, float j, struct f3 z)
{
	return a + b + c + d + e + f + g + h + i * 2 + y.a * 3 + y.b * 5 + y.c * 7 + j * 11 + z.a * 13 + z.c * 17;
}
