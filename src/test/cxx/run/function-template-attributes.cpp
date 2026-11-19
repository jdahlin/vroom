/* Function templates with GNU attributes on their symbols: always_inline,
   noinline, hidden visibility and noexcept with a dependent operand, as
   libc++'s __exchange ([temp.fct], [ext:gnu_attributes]). */
#include <stdio.h>

template <class T1, class T2 = T1>
[[__nodiscard__]] __attribute__((__always_inline__))
__attribute__((__visibility__("hidden"))) inline T1
exchange_value(T1 &obj, T2 &&v) noexcept(noexcept(T1(v)))
{
    T1 old = obj;
    obj = v;
    return old;
}

template <class T> __attribute__((noinline)) T twice(T x) { return x + x; }

int main()
{
    int a = 1;
    int old = exchange_value(a, 5);
    long l = 2;
    long lo = exchange_value(l, 7L);
    printf("%d %d %ld %ld %d %g\n", old, a, lo, l, twice(21), twice(1.5));
    return 0;
}
