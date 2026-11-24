/* [temp.static]/1: the definition of a class template's static data member
   outside the class, after a GNU attribute (libc++'s _LIBCPP_TEMPLATE_DATA_VIS) */
template <class C> struct B {
    typedef unsigned size_type;
    static const size_type npos = static_cast<size_type>(-1);
    static const size_type limit = 7;
};
template <class C>
__attribute__((__visibility__("default"))) const typename B<C>::size_type
    B<C>::npos;
template <class C>
__attribute__((__visibility__("default"))) const typename B<C>::size_type
    B<C>::limit;

int main()
{
    const unsigned *p = &B<char>::npos;
    const unsigned *q = &B<int>::limit;
    if (*p != 0xffffffffu)
        return 1;
    if (*q != 7)
        return 2;
    return 0;
}
