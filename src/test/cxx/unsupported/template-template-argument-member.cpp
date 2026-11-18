/* [temp.arg.template]/1: a member class template named by
   nested-name-specifier template identifier (libc++'s
   __xref<_Tp>::template __apply) is not looked up in its class yet */
template <class T> struct xref {
    template <class U> struct apply {
        typedef U type;
    };
};
template <template <class> class A> struct holder {
    typedef typename A<long>::type type;
};
template <class T> using held_t = typename holder<xref<T>::template apply>::type;
using direct_t = holder<xref<int>::template apply>::type;
int main()
{
    return 0;
}
