// [class.spaceship]/2, [class.compare.default]/6: defaulted auto operator<=>
// on a derived class with a base class and no data members of its own.
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

struct Base {
    unsigned data;
    constexpr auto operator<=>(const Base &) const noexcept = default;
};

struct Derived : Base {
    constexpr auto operator<=>(const Derived &) const noexcept = default;
};

int main()
{
    Derived a{1}, b{2}, c{1};
    if ((a <=> c).value != 0)
        return 1;
    if ((a <=> b).value != -1)
        return 2;
    if ((b <=> a).value != 1)
        return 3;
    return 0;
}
