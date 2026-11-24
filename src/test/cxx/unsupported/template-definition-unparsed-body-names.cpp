/* [dcl.meaning]/1: a function template's definition whose body the parser
   kept as tokens (a _BitInt type in it) declares its declarator-id only; the
   names of its return type (size_type) are used and stay what they were,
   so g's use of size_type is no error.  The one error is the declaration
   kept as tokens (libc++'s bitset<N>::count). */
typedef unsigned long size_type;

template <size_type N> struct B {
    size_type count() const;
};

template <size_type N>
size_type B<N>::count() const
{
    return N + static_cast<unsigned _BitInt(8)>(0);
}

template <size_type N> size_type g()
{
    size_type h = N;
    return h;
}

size_type v = g<4>();
