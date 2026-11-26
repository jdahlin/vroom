// [dcl.spec.auto]: deduce placeholder return type of a postponed hidden
// friend operator defined inside a class template (whose enclosing scope is
// the surrounding namespace, not the class).

namespace std {
struct strong_ordering {
    signed char value;
    static const strong_ordering less;
    static const strong_ordering equal;
    static const strong_ordering greater;
    friend constexpr bool operator<(strong_ordering a, int b)
    {
        return a.value < b;
    }
};
inline constexpr strong_ordering strong_ordering::less{-1};
inline constexpr strong_ordering strong_ordering::equal{0};
inline constexpr strong_ordering strong_ordering::greater{1};
} // namespace std

namespace ns {
struct Compare {
    template <typename TType, typename TBase>
    struct mixin {
        friend constexpr auto operator<=>(const TType &lhs, const TType &rhs)
        {
            if (lhs.value < rhs.value)
                return std::strong_ordering::less;
            if (rhs.value < lhs.value)
                return std::strong_ordering::greater;
            return std::strong_ordering::equal;
        }
    };
};

template <typename TBase, typename... TProps>
struct Strong : public TProps::template mixin<Strong<TBase, TProps...>, TBase>... {
    TBase value;
    constexpr explicit Strong(TBase v) : value(v) {}
};
} // namespace ns

using ID = ns::Strong<int, ns::Compare>;

int main()
{
    ID a(10), b(20);
    if (!(a < b))
        return 1;
    if (b < a)
        return 2;
    return 0;
}
