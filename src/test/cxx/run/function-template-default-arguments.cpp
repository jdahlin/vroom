/* Default arguments of a function template (libc++'s __libcpp_allocate's
   align = alignof(_Tp)): one per specialization, of the template parameters'
   types and values, used by calls that omit them ([dcl.fct.default]/1,
   [temp.inst]/12, [over.match.viable]/2). */
#include <stdio.h>
template <class T> T scaled(T x, int by = sizeof(T)) { return x * by; }
template <class T> T added(T x, T y = T(3)) { return x + y; }
int main(void)
{
    int failures = 0;
    long v = 5;
    char c = 2;
    if (scaled(v) != 40 || scaled(v, 2) != 10 || scaled(c) != 2)
        failures |= 1;
    if (added(v) != 8 || added(v, 1L) != 6)
        failures |= 2;
    printf("%d\n", failures);
    return failures;
}
