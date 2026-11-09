#include "va.h"

long vsum(int n, va_list ap)
{
	va_list ap2;
	long s = 0;

	va_copy(ap2, ap);	/* ap: a parameter */
	for (int i = 0; i < n; i++)
		s += va_arg(ap2, long);
	va_end(ap2);
	for (int i = 0; i < n; i++)
		s += 3 * va_arg(ap, long);
	return s;
}

long sum(int n, ...)
{
	va_list ap;
	long r;

	va_start(ap, n);
	r = vsum(n, ap);
	va_end(ap);
	return r;
}

long sum_twice(int n, ...)
{
	va_list ap, ap2;
	long r = 0;

	va_start(ap, n);
	va_copy(ap2, ap);	/* both locals */
	for (int i = 0; i < n; i++)
		r += va_arg(ap, long) * 10;
	r += vsum(n, ap2);
	va_end(ap2);
	va_end(ap);
	return r;
}
