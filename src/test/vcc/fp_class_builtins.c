/* __builtin_isinf, __builtin_isfinite, __builtin_signbit on float and double */
#include <stdio.h>

int main(void)
{
	volatile double d[] = { 0.0, -0.0, 1.5, -2.5, __builtin_inf(), -__builtin_inf(), __builtin_nan(""), -__builtin_nan(""), 1e-310 };
	volatile float f[] = { 0.0f, -0.0f, 1.5f, -2.5f, __builtin_inff(), -__builtin_inff(), __builtin_nanf(""), -__builtin_nanf(""), 1e-40f };

	for (int i = 0; i < 9; i++)
		printf("%d: %d %d %d | %d %d %d\n", i, __builtin_isinf(d[i]) != 0, __builtin_isfinite(d[i]) != 0,
		       __builtin_signbit(d[i]) != 0, __builtin_isinf(f[i]) != 0, __builtin_isfinite(f[i]) != 0,
		       __builtin_signbit(f[i]) != 0);
	return 0;
}
