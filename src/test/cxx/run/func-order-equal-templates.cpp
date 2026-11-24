/* [temp.func.order]/6, [over.match.best]/2: two unconstrained function
   templates with the same parameter types are equally specialized, so a call
   both can take is ambiguous; in a requires-expression that is a false
   requirement, not an unsupported analysis (libc++'s ranges::swap). */
namespace ns {
template <class T> void sw(T &, T &) = delete;
}
namespace other {
template <class T> struct ok { using type = void; };
template <class T> typename ok<T>::type sw(T &, T &) {}
template <class T, unsigned N> void sw(T (&)[N], T (&)[N]) {}
struct A {};
} // namespace other
using namespace ns;
template <class T, class U>
concept swappable_by_adl = requires(T &&t, U &&u) {
    sw(static_cast<T &&>(t), static_cast<U &&>(u));
};
static_assert(!swappable_by_adl<other::A &, other::A &>);
static_assert(swappable_by_adl<other::A (&)[2], other::A (&)[2]>);
int main()
{
    other::A a[2], b[2];
    using other::sw;
    sw(a, b);
    return 0;
}
