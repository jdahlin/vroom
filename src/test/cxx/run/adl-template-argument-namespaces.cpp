/* [basic.lookup.argdep]/2: a class template specialization's associated
   namespaces include those of its template type arguments and of its
   template template arguments (unit-concepts' swap of two json iterators,
   found in std through basic_json's arguments) */
namespace arg
{
struct A {
};
template <class T> struct Holder {
};
template <class T> int probe(const T &)
{
    return 1;
}
} // namespace arg
namespace temp
{
template <class T> struct Box {
    int v;
};
template <template <class> class H> struct With {
};
} // namespace temp
namespace pack
{
struct P {
};
template <class T> int packed(const T &)
{
    return 3;
}
} // namespace pack
namespace other
{
template <class... T> struct List {
};
} // namespace other

int main()
{
    temp::Box<arg::A> a{0};
    temp::Box<temp::Box<arg::A> *> nested{0};
    temp::With<arg::Holder> with;
    other::List<int, pack::P> list;
    if (probe(a) != 1)
        return 1;
    if (probe(nested) != 1)
        return 2;
    if (probe(with) != 1)
        return 3;
    if (packed(list) != 3)
        return 4;
    return 0;
}
