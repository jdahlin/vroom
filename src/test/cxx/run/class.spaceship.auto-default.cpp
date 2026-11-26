// [class.spaceship]/2: defaulted auto operator<=> deduces its comparison
// category (e.g. std::strong_ordering).
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

struct Bits {
    unsigned data;
    constexpr auto operator<=>(const Bits &) const noexcept = default;
};

int main()
{
    Bits a{1}, b{2}, c{1};
    if ((a <=> c).value != 0)
        return 1;
    if ((a <=> b).value != -1)
        return 2;
    if ((b <=> a).value != 1)
        return 3;
    return 0;
}
