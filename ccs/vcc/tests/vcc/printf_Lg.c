/* %Lg of long doubles computed at run time (rt's __addtf3/__multf3) */
#include <stdio.h>
volatile long double a = 19, b = 9e-13L, c = 0.5L;
__attribute__((noinline)) long double f(void) { return 19.0000000000009L; }
int main(void)
{
	long double r = a + b, s = a * c + a * c, t = r * c;
	printf("%Lg %Lg %Lg %Lg %Lg\n", f(), r, s, t, 1e300L * 10);
	printf("%.17g %.17g\n", (double)r, (double)t);
	return 0;
}
