// [basic.lookup.argdep]/3: unqualified lookup finds nothing, argument-
// dependent lookup finds two function templates of the argument's
// namespace, in a partial specialization's decltype (doctest's
// has_insertion_operator).
template <class T> T &&declval();
namespace N {
struct X {
    int v;
};
struct Out {
    int sink;
};
template <class T> Out &operator<<(Out &o, const T &) { return o; }
template <class T> Out &operator<<(Out &o, T *) { return o; }
} // namespace N
template <class T, class = void> struct has_ins {
    static const int value = 0;
};
template <class T>
struct has_ins<T, decltype(operator<<(declval<N::Out &>(), declval<const T &>()), void())> {
    static const int value = 1;
};
struct Y {};
int main()
{
    if (has_ins<N::X>::value != 1)
        return 1;
    return 0;
}
