// [class.pre]/2: the injected-class-name is declared in the class's own
// scope.  A member template of a class template's specialization made after
// the template (libc++'s allocator<int>, named by vector's layout) names the
// template by it: the lookup found it, but took it for a namespace member
// declared after the template ([temp.res]/10) and refused it.
namespace s {
inline namespace v {
template <class T, T v> struct ic { static constexpr T value = v; };
template <class T>
struct nothrow_copy : ic<bool, __is_nothrow_constructible(T, const T &)> {};
template <class T> class al;
template <bool, class U> struct base_if {};
template <class T> class al : base_if<true, al<T>> {
public:
    al() noexcept = default;
    template <class U> al(const al<U> &) noexcept {}
};
template <class A> struct layout {
    layout() noexcept(nothrow_copy<A>::value) {}
};
template <class T, class A = al<T>> struct vec {
    layout<A> l;
};
} // namespace v
} // namespace s
int f(s::vec<int> &x) { return sizeof(x); }
int main() { return 0; }
