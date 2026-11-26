// Partial specialization whose argument uses its own pack expansion to cover
// non-pack parameters before a primary template's pack (and templates without
// a pack parameter), as in std::hash<StrongType::Typedef<TBase, TProps...>>.

namespace ns {
template <typename TBase, typename TTag, typename... TProps>
struct Typedef {
    TBase value;
    constexpr TBase base() const noexcept { return value; }
};

template <typename A, typename B>
struct Pair {
    A first;
    B second;
};
} // namespace ns

template <typename T>
struct Hash;

template <typename TBase, typename... TProps>
struct Hash<ns::Typedef<TBase, TProps...>> {
    int operator()(const ns::Typedef<TBase, TProps...> &t) const noexcept
    {
        return static_cast<int>(t.base()) + static_cast<int>(sizeof...(TProps));
    }
};

template <typename... Ts>
struct Hash<ns::Pair<Ts...>> {
    int operator()(const ns::Pair<Ts...> &p) const noexcept
    {
        return p.first + p.second + static_cast<int>(sizeof...(Ts));
    }
};

struct Tag1 {};
struct Tag2 {};
struct PropA {};
struct PropB {};

int main()
{
    ns::Typedef<int, Tag1> a{10};
    ns::Typedef<int, Tag2, PropA, PropB> b{20};
    ns::Pair<int, int> p{3, 4};

    if (Hash<ns::Typedef<int, Tag1>>{}(a) != 11)
        return 1;
    if (Hash<ns::Typedef<int, Tag2, PropA, PropB>>{}(b) != 23)
        return 2;
    if (Hash<ns::Pair<int, int>>{}(p) != 9)
        return 3;
    return 0;
}
