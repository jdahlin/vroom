#include <stdarg.h>
#include "va.h"

long take_long(int n, ...)
{
	va_list ap;
	long s = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		s += va_arg(ap, long);
	va_end(ap);
	return s;
}

/* sqlite3_db_config(db, op, 1, 0): an int 0 read back as int * (Apple's
 * variadic slots are 8 bytes; clang writes all of them) */
int null_ptr(int n, ...)
{
	va_list ap;
	int nonnull = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		(void)va_arg(ap, int);
		nonnull += va_arg(ap, int *) != 0;
	}
	va_end(ap);
	return nonnull;
}
