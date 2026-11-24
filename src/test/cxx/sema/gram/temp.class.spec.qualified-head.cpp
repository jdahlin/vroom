// [temp.spec.partial]/1: a partial specialization of a namespace member
// template written with a qualified class-head-name.
namespace N {
template <class T> struct V;
}
template <class T> struct N::V<T *> {
    static constexpr int k = 1;
};
template <> struct N::V<int> {
    static constexpr int k = 2;
};
static_assert(N::V<char *>::k == 1);
static_assert(N::V<int>::k == 2);
