/* [temp.inst]/2, [temp.variadic]/7: a specialization named in the
   template-arguments of a nested-name-specifier's template-id is not
   instantiated; with packs of different lengths its definition would be
   ill-formed.  [temp.variadic]/5: the packs of a member class template of a
   class template's instance */
template <bool, class A, class B>
struct choose {
    typedef A type;
};
template <class A, class B>
struct choose<false, A, B> {
    typedef B type;
};
template <class...>
struct all {
    static const bool value = true;
};
template <class B0, class... Bp>
struct all<B0, Bp...> : choose<B0::value, all<Bp...>, B0>::type {};
template <bool V>
struct flag {
    static const bool value = V;
};
template <class A, class B>
struct pair_ok {
    static const bool value = true;
};
template <class... T>
struct holder {
    int which;
    template <class... U>
    struct matching : all<flag<true>, pair_ok<T, U>...> {};
    template <class... U>
    struct sized {
        int n[sizeof...(U) + 1];
    };
    template <class... U>
    struct check {
        static const bool value =
            all<flag<sizeof...(U) == sizeof...(T)>, matching<U...> >::value;
    };
    holder() : which(1) {}
    template <class... U>
    holder(U...) : which(2)
    {
    }
};
int main()
{
    holder<int, long> a;
    if (holder<int, long>::check<int>::value)
        return 1;
    if (!holder<int, long>::check<int, long>::value)
        return 2;
    holder<int, long>::sized<char, char> s;
    if (sizeof(s.n) != 3 * sizeof(int))
        return 3;
    if (a.which != 1)
        return 4;
    return 0;
}
