/* cxx-run-flags: -std=c++20 */
/* [class.spaceship]/2: a defaulted operator<=> with a comparison category
   as its return type: the bases and members in order, the first
   comparison that is not equal is the result, else equal; member or
   friend. */
namespace std {
struct strong_ordering {
    signed char v;
    static const strong_ordering less, equal, equivalent, greater;
    friend constexpr bool operator==(strong_ordering a, int) { return a.v == 0; }
    friend constexpr bool operator<(strong_ordering a, int) { return a.v < 0; }
    friend constexpr bool operator>(strong_ordering a, int) { return a.v > 0; }
    friend constexpr bool operator!=(strong_ordering a, int) { return a.v != 0; }
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0},
    strong_ordering::equivalent{0}, strong_ordering::greater{1};
}
struct word {
    int v;
    constexpr std::strong_ordering operator<=>(const word &o) const
    {
        if (v < o.v)
            return std::strong_ordering::less;
        if (v > o.v)
            return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }
};
struct point {
    word x;
    word y;
    std::strong_ordering operator<=>(const point &) const = default;
};
struct segment : point {
    word steps[2];
    friend std::strong_ordering operator<=>(const segment &, const segment &) = default;
};
int main()
{
    segment a{{{1}, {2}}, {{1}, {2}}}, b{{{1}, {2}}, {{1}, {3}}},
        c{{{1}, {2}}, {{1}, {2}}}, d{{{1}, {1}}, {{9}, {9}}};
    return !((a <=> b) < 0) + !((b <=> a) > 0) + !((a <=> c) == 0) +
           !((a <=> d) > 0);
}
