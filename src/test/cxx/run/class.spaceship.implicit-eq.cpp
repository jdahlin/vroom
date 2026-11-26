// [class.spaceship]/3: a defaulted operator<=> implicitly declares a defaulted
// operator== when the class does not declare one.

namespace std {
struct strong_ordering {
    signed char value;
    static const strong_ordering less;
    static const strong_ordering equal;
    static const strong_ordering equivalent;
    static const strong_ordering greater;
    friend constexpr bool operator==(strong_ordering v, int) noexcept
    {
        return v.value == 0;
    }
    friend constexpr bool operator<(strong_ordering v, int) noexcept
    {
        return v.value < 0;
    }
    friend constexpr bool operator>(strong_ordering v, int) noexcept
    {
        return v.value > 0;
    }
    friend constexpr bool operator<=(strong_ordering v, int) noexcept
    {
        return v.value <= 0;
    }
    friend constexpr bool operator>=(strong_ordering v, int) noexcept
    {
        return v.value >= 0;
    }
};
inline constexpr strong_ordering strong_ordering::less{-1};
inline constexpr strong_ordering strong_ordering::equal{0};
inline constexpr strong_ordering strong_ordering::equivalent{0};
inline constexpr strong_ordering strong_ordering::greater{1};
} // namespace std

struct Source {
    unsigned short id;
    unsigned char type;
    auto operator<=>(const Source &other) const = default;
};

int main()
{
    Source a{10, 1};
    Source b{10, 1};
    Source c{10, 2};
    if (!(a == b) || (a != b))
        return 1;
    if ((a == c) || !(a != c))
        return 2;
    if (!(a < c) || (c < a))
        return 3;
    return 0;
}
