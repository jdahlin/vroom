/* [temp.static]/1: "A definition for a static data member or static data
   member template may be provided in a namespace scope enclosing the
   definition of the static member's class": of a class template, with its
   own names for the template parameters (libc++'s
   __deque_iterator::__block_size) */
template <class D, D n> struct W {
    static const D value = n;
};
template <class V, class D, D BS = W<D, 5>::value> struct It {
    typedef D diff;
    static const diff bs;
    static V first;
    static int count;
};
/* (the names of the class's parameters in the initializer) */
template <class A, class B, B BS> const B It<A, B, BS>::bs = BS + 1;
template <class V, class D, D BS> V It<V, D, BS>::first = V(BS);
/* (the definition's own names, none in the initializer) */
template <class A, class B, B Size> int It<A, B, Size>::count = 7;
int main()
{
    if (It<long, int>::bs != 6)
        return 1;
    if (It<long, int, 4>::bs != 5)
        return 2;
    if (It<char, int, 3>::first != 3)
        return 3;
    return It<int, long, 1>::count - 7;
}
