/* [abi:expressions]: a member template call on a resolved specialization in a
   return type (json's get): L_Z, dt and tl */
namespace detail {
template <int N> struct tag {};
}
using detail::tag;
template <class T> T &&dv();
template <class A> struct J {
    using self = J<A>;
    template <class V> V impl(tag<1>) const { return V(); }
    template <class V, class U = V> auto get() const -> decltype(dv<const self &>().template impl<V>(detail::tag<1>{})) { return impl<V>(tag<1>{}); }
};
int main() { J<char> j; return j.get<int>(); }
