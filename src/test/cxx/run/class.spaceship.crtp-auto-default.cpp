// [dcl.spec.auto]/11, [class.spaceship]/2: calling an overloaded operator
// with a placeholder return type (auto operator<=> = default on a CRTP base
// class template specialization) deduces its return type on demand.
namespace std {
struct strong_ordering {
    signed char value;
    static const strong_ordering less;
    static const strong_ordering equal;
    static const strong_ordering equivalent;
    static const strong_ordering greater;
    friend constexpr bool operator==(strong_ordering a, int b) noexcept
    {
        return a.value == b;
    }
};
inline constexpr strong_ordering strong_ordering::less{-1};
inline constexpr strong_ordering strong_ordering::equal{0};
inline constexpr strong_ordering strong_ordering::equivalent{0};
inline constexpr strong_ordering strong_ordering::greater{1};
} // namespace std

template <typename Impl, typename T>
struct BaseBitSet {
    T data;
    constexpr BaseBitSet(T v = 0) : data(v) {}
    constexpr auto operator<=>(const BaseBitSet &) const noexcept = default;
};

struct CompanyMask : public BaseBitSet<CompanyMask, unsigned short> {
    constexpr CompanyMask(unsigned short v = 0)
        : BaseBitSet<CompanyMask, unsigned short>(v)
    {
    }
    constexpr auto operator<=>(const CompanyMask &) const noexcept = default;
};

int main()
{
    CompanyMask a(1), b(2), c(1);
    if ((a <=> c).value != 0)
        return 1;
    if ((a <=> b).value != -1)
        return 2;
    if ((b <=> a).value != 1)
        return 3;
    return 0;
}
