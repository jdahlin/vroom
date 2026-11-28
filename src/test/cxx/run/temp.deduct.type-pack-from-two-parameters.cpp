/* [temp.deduct.type]/2: a template parameter pack deduced both by another
   parameter and by a function parameter pack, the same from both; an alias
   template-id with a pack expansion that is a typename-specifier's member
   is a non-deduced context ([temp.deduct.type]/5.1) */
template <class T> struct type_identity { using type = T; };
template <class... A> struct fs {
    int s;
    constexpr fs(int x) : s(x) {}
};
template <class... A>
using format_string = typename type_identity<fs<A...>>::type;
template <class... A> struct gs {};
template <class... A> int count(gs<A...> *, A &&...) { return sizeof...(A); }
template <class... A> constexpr int format(format_string<A...> f, A &&...)
{
    return sizeof...(A) + f.s;
}
template <typename A, typename... Args>
consteval int print(format_string<A, Args...> fm, A a, Args... o)
{
    return format(fm, static_cast<A &&>(a), static_cast<Args &&>(o)...);
}
int main()
{
    int x = 1;
    constexpr int one = print(1, 2), two = print(1, 2, 3);
    gs<int &> g;
    gs<int> h;
    if (count(&g, x) != 1 || count(&h, 1) != 1)
        return 1;
    if (one != 2 || two != 3)
        return 2;
    return 0;
}
