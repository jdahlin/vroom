/* [temp.arg.template]/1, [temp.names]/5: nested-name-specifier template
   identifier as a template-argument names a template (libc++'s
   __xref<_Tp>::template __apply in common_reference.h); the alias template
   is only declared (its use is template-template-argument-member.cpp's in
   test/cxx/unsupported) */
template <class T> struct xref {
    template <class U> struct apply {
        typedef U type;
    };
};
template <template <class> class A> struct holder {
    typedef typename A<long>::type type;
};
template <class T> using held_t = typename holder<xref<T>::template apply>::type;
int main()
{
    return 0;
}
