/* [temp.class.order], [temp.constr.order]: of partial specializations with
   the same arguments the one whose constraints subsume the other's is more
   specialized, through concept definitions (libc++'s
   __transform_view_iterator_concept) */
namespace lib
{
inline namespace v1
{
template <class T> using it_t = typename T::it;
template <class I>
concept in = requires { typename I::in; };
template <class I>
concept fi = in<I> && requires { typename I::f; };
template <class I>
concept bi = fi<I> && requires { typename I::b; };
namespace ranges
{
template <class T>
concept range = requires { typename T::it; };
template <class T>
concept inr = range<T> && in<it_t<T>>;
template <class T>
concept fr = inr<T> && fi<it_t<T>>;
template <class T>
concept br = fr<T> && bi<it_t<T>>;
} // namespace ranges
} // namespace v1
} // namespace lib
template <class V> struct C {
    static constexpr int v = 0;
};
template <lib::ranges::fr V> struct C<V> {
    static constexpr int v = 1;
};
template <lib::ranges::br V> struct C<V> {
    static constexpr int v = 2;
};
template <class V> struct D {
    static constexpr int v = 0;
};
template <class V>
    requires lib::ranges::inr<V> && lib::ranges::fr<V>
struct D<V> {
    static constexpr int v = 1;
};
template <class V>
    requires lib::ranges::inr<V> && lib::ranges::br<V>
struct D<V> {
    static constexpr int v = 2;
};
struct I {
    struct in {
    };
    struct f {
    };
    struct b {
    };
};
struct F {
    struct in {
    };
    struct f {
    };
};
struct T {
    using it = I;
};
struct U {
    using it = F;
};
int main()
{
    return C<T>::v != 2 || C<U>::v != 1 || C<int>::v != 0 || D<T>::v != 2 ||
           D<U>::v != 1;
}
