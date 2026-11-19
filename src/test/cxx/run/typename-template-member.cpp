/* typename T::template X<U> with a dependent T ([temp.res]/5,
   [temp.names]/5): a dependent type, the member template looked up in the
   class substituted for T.  As libc++'s __pointer_traits_rebind: the
   primary when looking it up fails ([temp.deduct]/11).  (A class with a
   member template is not analysed yet, so no use finds one.) */
#include <stdio.h>

template <class...> using void_t = void;

template <class T, class U, class = void> struct rebind {
    static const int which = 1;
};
template <class T, class U>
struct rebind<T, U, void_t<typename T::template other<U> > > {
    typedef typename T::template other<U> type;
    static const int which = 2;
};

struct without {};

template <class T> struct holder {
    typedef typename T::template other<long> type;
    type member;
};

int main(void)
{
    int failures = 0;
    if (rebind<without, int>::which != 1)
        failures |= 1;
    if (rebind<int *, int>::which != 1 || rebind<int, char>::which != 1)
        failures |= 2;
    printf("%d\n", failures);
    return failures;
}
