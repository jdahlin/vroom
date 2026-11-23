/* An alias template declared twice with the same tokens (libc++'s
   __swap_result_t in is_swappable.h and swap.h), between them a function
   template's declaration whose return type names it: the alias stays
   usable ([temp.alias]/1, [temp.fct]/1). */
#include <stdio.h>

template <class T> struct id {
    typedef T type;
};
template <class T> using id_t = typename id<T>::type;

template <class T> id_t<T> twice(T x);

template <class T> using id_t = typename id<T>::type;

template <class T> id_t<T> twice(T x) { return x * 2; }

int main(void)
{
    int failures = 0;
    id_t<long> v = 21;
    int x = 4;
    if (twice(v) != 42)
        failures |= 1;
    if (twice(x) != 8)
        failures |= 2;
    printf("%d\n", failures);
    return failures;
}
