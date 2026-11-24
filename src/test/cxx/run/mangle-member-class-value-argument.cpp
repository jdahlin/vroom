/* [abi:mangle.template-arg]: a value-dependent template argument that names a
   member of a member class of a class template specialization,
   C<args>::type::name (sr <template-id> <level> E <name>). */
template <bool B, class T = void> struct enable { };
template <class T> struct enable<true, T> { typedef T type; };
template <class T> struct flag { static const bool value = false; };
template <class P, class Q> struct combine {
    typedef P type;
    static const bool value = true;
};
struct One { static const int den = 1; };

template <class Rep, class P> struct D {
    typedef int rep;
    typedef P period;
    template <class R2, class P2,
              typename enable<combine<P2, period>::value &&
                                  (flag<rep>::value ||
                                   (combine<P2, period>::type::den == 1 &&
                                    !flag<R2>::value)),
                              int>::type = 0>
    D(const D<R2, P2> &)
    {
    }
    D() {}
};

int main()
{
    D<int, One> a;
    D<long, One> b(a);
    (void)b;
    return 0;
}
