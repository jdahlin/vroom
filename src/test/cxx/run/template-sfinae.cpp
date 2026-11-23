/* Substitution failure is not an error ([temp.deduct]/8): a candidate
   whose declaration names a member its argument's class does not have
   (in the return type, a parameter type or a default template-argument)
   is no candidate, and another template or function of the name is
   called; an explicit specialization of a class template giving the
   member only for some arguments selects between overloads. */
#include <stdio.h>

struct with_type {
    struct type {
        int v;
    };
};
struct without_type {
    int y;
};

template <class T> typename T::type probe(T, int)
{
    typename T::type r;
    r.v = 10;
    return r;
}
template <class T> long probe(T, long) { return 20; }

template <class T, class U = typename T::type> int dflt(T) { return 1; }
template <class T> long dflt(T *) { return 2; }

template <class T> int param(T, typename T::type *) { return 1; }
template <class T> int param(T, void *) { return 2; }

template <class T> struct only_int {};
template <> struct only_int<int> {
    struct type {};
};

template <class T> typename only_int<T>::type *gate(T, int) { return 0; }
template <class T> int gate(T, long) { return 7; }

int plain(int) { return 3; }
template <class T> typename T::type plain(T t) { return t; }

int main(void)
{
    int fail = 0;
    with_type w;
    without_type wo;
    if (probe(w, 0).v != 10)
        fail |= 1;
    if (probe(wo, 0) != 20)
        fail |= 2;
    if (dflt(w) != 1)
        fail |= 4;
    if (dflt(&wo) != 2)
        fail |= 8;
    if (param(w, (with_type::type *)0) != 1)
        fail |= 16;
    if (param(wo, (void *)0) != 2)
        fail |= 32;
    if (gate(1, 0) != 0)
        fail |= 64;
    if (gate(1.0, 0) != 7)
        fail |= 128;
    if (plain(4) != 3)
        fail |= 256;
    printf("%d\n", fail);
    return fail;
}
