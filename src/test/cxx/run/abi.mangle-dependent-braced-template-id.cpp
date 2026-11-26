/* [abi:expressions]: tl <type> E, an empty braced functional cast to a
   specialization that depends on a template parameter, qualified or not,
   in a decltype in a return type */
namespace detail {
template <class T> struct identity_tag {};
}
template <class T> struct tag {};
template <class T> struct S {
    T v;
};
template <class T> T impl(const S<T> &s, detail::identity_tag<T>)
{
    return s.v;
}
template <class T> T other(const S<T> &s, tag<T *>) { return s.v; }

template <class T>
auto from(const S<T> &s) -> decltype(impl(s, detail::identity_tag<T>{}))
{
    return impl(s, detail::identity_tag<T>{});
}
template <class T>
auto from_tag(const S<T> &s) -> decltype(other(s, tag<T *>{}))
{
    return other(s, tag<T *>{});
}

int main()
{
    S<int> s{3};
    return from(s) + from_tag(s) - 6;
}
