/* [dcl.struct.bind]/4: get<i> beside get<T> overloads, which an index
   rules out (std::pair) */
typedef decltype(sizeof 0) size_type;
namespace std
{
template <class T> struct tuple_size;
template <size_type I, class T> struct tuple_element;
struct pair {
    int first;
    long second;
};
template <> struct tuple_size<pair> {
    static constexpr size_type value = 2;
};
template <> struct tuple_element<0, pair> {
    using type = int;
};
template <> struct tuple_element<1, pair> {
    using type = long;
};
template <size_type I> typename tuple_element<I, pair>::type &get(pair &p)
{
    if constexpr (I == 0)
        return p.first;
    else
        return p.second;
}
template <size_type I> typename tuple_element<I, pair>::type &&get(pair &&p)
{
    return static_cast<typename tuple_element<I, pair>::type &&>(get<I>(p));
}
template <class T> int &get(pair &p)
{
    return p.first;
}
} // namespace std
int main()
{
    std::pair p{1, 2};
    auto [a, b] = p;
    auto &[c, d] = p;
    c = 5;
    return !(a == 1 && b == 2 && d == 2 && p.first == 5);
}
