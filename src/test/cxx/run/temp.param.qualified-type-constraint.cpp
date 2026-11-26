// [temp.param]/4: namespace-qualified type-constraint on an abbreviated
// function template parameter (e.g. `const std::integral auto &val` in
// OpenTTD core/pool_type.hpp:78).

namespace my {
inline namespace v1 {
template <typename T>
concept integral = __is_integral(T);
}
} // namespace my

struct Tag {
    int v;
};

constexpr Tag operator+(const my::integral auto &a, const Tag &b)
{
    return {static_cast<int>(a) + b.v};
}

struct Other {
    int v;
    friend constexpr Other operator+(const Other &a, const Other &b)
    {
        return {a.v + b.v};
    }
};

int main()
{
    constexpr Tag t = 10 + Tag{5};
    static_assert(t.v == 15);
    volatile int x = 3;
    Other o = Other{x} + Other{4};
    if (o.v != 7)
        return 1;
    return 0;
}
