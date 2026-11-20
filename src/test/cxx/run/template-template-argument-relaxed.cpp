/* [temp.arg.template]/3 (P0522R0): an alias template, a class template
   with default arguments and a pack-parameter template as the argument
   of a template template-parameter */
template <class T> struct V { T t; };
template <class T, class U = int> struct W { T t; U u; };
template <class T> using A = V<T>;
template <class...> struct P { static int n() { return 1; } };
template <template <class...> class TT, class... As> struct Ap {
    using type = TT<As...>;
};
template <template <class> class TT> struct One { using type = TT<char>; };
template <template <class...> class TT> struct Two {
    using type = TT<int, long>;
};
int main()
{
    int r = 0;
    if (sizeof(Ap<V, int>::type) != 4)
        r |= 1;
    if (sizeof(Ap<A, int>::type) != 4)
        r |= 2;
    if (sizeof(Ap<W, int, long>::type) != 16)
        r |= 4;
    if (sizeof(One<A>::type) != 1)
        r |= 8;
    if (sizeof(One<W>::type) != 8)
        r |= 16;
    if (Two<P>::type::n() != 1)
        r |= 32;
    return r;
}
