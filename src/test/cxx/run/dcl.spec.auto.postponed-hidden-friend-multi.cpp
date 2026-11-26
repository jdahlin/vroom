// [dcl.spec.auto]: deducing placeholder return type of a postponed hidden
// friend operator when multiple specializations of the same class template
// have postponed bodies for the same syntax node.

struct Ops {
    template <typename TType, typename TBaseType> struct mixin {
        friend constexpr auto operator+(const TType &lhs, const TType &rhs)
        {
            return TType(lhs.value + rhs.value);
        }
    };
};

template <typename TBaseType, typename TTag, typename... TProperties>
struct Typedef : public TProperties::template mixin<
                     Typedef<TBaseType, TTag, TProperties...>, TBaseType>... {
    explicit constexpr Typedef(TBaseType v) : value(v) {}
    friend struct Ops;
    TBaseType value{};
};

struct Tag1;
struct Tag2;
using Tile = Typedef<unsigned, Tag1, Ops>;
using Year = Typedef<int, Tag2, Ops>;

int main()
{
    Tile t1{1}, t2{2};
    Year y1{1950}, y2{50};
    if ((y1 + y2).value != 2000)
        return 1;
    if ((t1 + t2).value != 3)
        return 2;
    return 0;
}
