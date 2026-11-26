// Defaulted operator<=> on a class with a private member whose operator<=>
// is a function template with deduced auto return type (as in
// EncodedString::operator<=> comparing private std::string).

namespace std {
struct strong_ordering {
    signed char value;
    static const strong_ordering less;
    static const strong_ordering equal;
    static const strong_ordering greater;
    friend constexpr bool operator==(strong_ordering a, int b)
    {
        return a.value == b;
    }
    friend constexpr bool operator<(strong_ordering a, int b)
    {
        return a.value < b;
    }
};
inline constexpr strong_ordering strong_ordering::less{-1};
inline constexpr strong_ordering strong_ordering::equal{0};
inline constexpr strong_ordering strong_ordering::greater{1};
} // namespace std

template <typename T>
struct Box {
    T v;
};

template <typename T>
constexpr auto operator<=>(const Box<T> &a, const Box<T> &b) noexcept
{
    if (a.v < b.v)
        return std::strong_ordering::less;
    if (b.v < a.v)
        return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

class Wrapper {
    Box<int> b;

public:
    constexpr Wrapper(int v) : b{v} {}
    auto operator<=>(const Wrapper &) const = default;
};

int main()
{
    Wrapper a(1), b(2), c(1);
    if (!(a < b))
        return 1;
    if (b < a)
        return 2;
    if (!((a <=> c) == 0))
        return 3;
    return 0;
}
