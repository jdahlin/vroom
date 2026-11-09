/* va_copy and va_end on a va_list parameter and a local one: Apple's va_list
 * is a char *, so both can be register variables at -O1 and up */
#include <stdarg.h>

long vsum(int n, va_list ap);
long sum(int n, ...);
long sum_twice(int n, ...);
