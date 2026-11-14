/* vcc-flags: -std=c23 */
#include <stdarg.h>
static int total(int n, ...) {
    va_list ap;
    va_start(ap,n);
    int sum=0;
    for (int i=0;i<n;i++) sum += va_arg(ap,int);
    va_end(ap);
    return sum;
}
static int optional(int n, ...) {
    va_list ap;
    int count = 0;
    __builtin_c23_va_start(ap, count++);
    int x = va_arg(ap,int);
    va_end(ap);
    return count ? -1 : x;
}
static int first(...) {
    va_list ap;
    __builtin_c23_va_start(ap);
    int x = va_arg(ap,int);
    va_end(ap);
    return x;
}
int main(void) { return total(4,3,5,7,11) != 26 || first(17) != 17 || optional(1,19) != 19; }
