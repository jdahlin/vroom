// [over.match.class.deduct]/1: an implicit guide has no exception
// specification (one naming a class member must not be looked at for it).
// [abi:expressions]: a generic lambda's requires-clause naming its
// parameters in simple and compound requirements.
template <class T> struct trait {
    static constexpr bool value = true;
};

template <class A, class B> struct pair_of {
    using first_type = A;
    A first;
    B second;
    template <bool D = true>
    pair_of(A const &a, B const &b) noexcept(trait<first_type>::value)
        : first(a), second(b)
    {
    }
};

template <class T> auto make(T t) { return pair_of(t, t); }

template <class T> int use(T t)
{
    auto p = make(t);
    return p.first + p.second;
}

template <class T> concept any = true;

inline constexpr auto less = []<class T, class U>(const T &t, const U &u)
    requires requires {
        t < u;
        { u < t } -> any;
    }
{ return t < u; };

int main()
{
    volatile int one = 1, two = 2;
    int a = one, b = two;
    if (use(1) != 2)
        return 1;
    if (!less(a, b) || less(b, a))
        return 2;
    return 0;
}
