/* [temp.over.link]/6, [temp.param]/10: a class template defined after a
   declaration with a default, the head with a non-type parameter of a
   dependent type (libc++'s __bit_iterator); and [[gnu::abi_tag]] on the
   class-head of a class template (libc++'s _LIBCPP_HIDE_STRUCT_FROM_ABI) */
template <class C, bool B = false, typename C::T = 0> class It;
template <class C> struct R {
    friend class It<C, false>;
};
template <class C, bool B, typename C::T> class It {
public:
    int get() const { return B; }
};
struct Q {
    typedef int T;
};
template <class C, typename C::T V = 3> struct Two;
template <class C, typename C::T V> struct Two {
    static const int value = V;
};

template <class T> class [[gnu::abi_tag("tagged")]] Tagged {
public:
    int size() const { return sizeof(T); }
};

int main()
{
    It<Q, true> a;
    It<Q> b;
    if (a.get() != 1 || b.get() != 0)
        return 1;
    if (Two<Q>::value != 3 || Two<Q, 5>::value != 5)
        return 2;
    Tagged<long> t;
    if (t.size() != sizeof(long))
        return 3;
    return 0;
}
