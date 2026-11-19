/* [dcl.pre]/10, [temp.res.general]/6: a static_assert-declaration in a
   class template's member-specification whose condition is value-dependent
   is checked per instantiation (libc++'s ratio, one condition a variable
   template, `>` after its template-id) */
#include <stdio.h>
template <long N> constexpr long abs_v = N < 0 ? -N : N;
template <long N, long D = 1>
struct ratio {
    static_assert(abs_v<N> >= 0, "numerator out of range");
    static_assert(D != 0, "divide by 0");
    static_assert(abs_v<D> > 0, "denominator out of range");
    static constexpr long num = abs_v<N>;
    static constexpr long den = D;
};
struct plain {
    static_assert(sizeof(int) == 4, "int");
    int x;
};
int main()
{
    plain p = {5};
    printf("%ld %ld %ld %d\n", ratio<-3, 2>::num, ratio<-3, 2>::den,
           ratio<7>::den, p.x);
    return 0;
}
