// [expr.const]: constant evaluation calling a postponed constexpr hidden
// friend operator defined in a class template specialization.

struct Integer {
    template <typename TType, typename TBaseType> struct mixin {
        friend constexpr TType operator-(const TType &lhs, const TBaseType &rhs)
        {
            return TType(lhs.value - rhs);
        }
    };
};

template <typename TBaseType, typename TTag, typename... TProperties>
struct Typedef : public TProperties::template mixin<
                     Typedef<TBaseType, TTag, TProperties...>, TBaseType>... {
    constexpr Typedef() = default;
    constexpr Typedef(const Typedef &) = default;
    explicit constexpr Typedef(const TBaseType &value) : value(value) {}
    friend struct Integer;
    TBaseType value{};
};

struct Tag;
using Year = Typedef<int, Tag, Integer>;

struct S {
    static constexpr Year A{2051};
    static constexpr Year B{A - 1};
};

static_assert(S::B.value == 2050);

int main()
{
    const Year *volatile p = &S::B;
    if (p->value != 2050)
        return 1;
    return 0;
}
