/* [dcl.struct.bind]/4: get<i>(e) of a tuple-like binding is looked up in
   the associated namespaces, which a template argument adds to: the
   templates of each take part (unit-items' iteration_proxy_value, whose
   namespaces include std) */
using size = decltype(sizeof 0);
namespace std
{
template <class T> struct tuple_size;
template <size I, class T> struct tuple_element;
} // namespace std

namespace other
{
struct Tag {
};
template <size I, class T> int get(const T &, int, int)
{
    return -1;
}
} // namespace other
namespace lib
{
template <class T> struct Pair {
    int a, b;
};
template <size I, class T> int get(const Pair<T> &p)
{
    if (I == 0)
        return p.a;
    return p.b;
}
} // namespace lib
namespace std
{
template <class T> struct tuple_size<lib::Pair<T>> {
    static constexpr ::size value = 2;
};
template <size I, class T> struct tuple_element<I, lib::Pair<T>> {
    using type = int;
};
} // namespace std

int main()
{
    lib::Pair<other::Tag> p{3, 4};
    auto [x, y] = p;
    if (x != 3 || y != 4)
        return 1;
    return 0;
}
