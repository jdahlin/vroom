// [class.eq]/3, [class.spaceship]/2: a defaulted comparison of a class with
// no subobject: == is true, <=> is equal; and the operators declared after
// it on unscoped enumerators in an enum's definition, which an opaque
// declaration redeclares ([dcl.enum]/3).
// std: c++20
namespace std {
struct strong_ordering {
    signed char v;
    static const strong_ordering less, equal, equivalent, greater;
    friend constexpr bool operator==(strong_ordering a, int) { return a.v == 0; }
    friend constexpr bool operator<(strong_ordering a, int) { return a.v < 0; }
    friend constexpr bool operator==(strong_ordering a, strong_ordering b) { return a.v == b.v; }
};
inline constexpr strong_ordering strong_ordering::less{-1},
    strong_ordering::equal{0}, strong_ordering::equivalent{0},
    strong_ordering::greater{1};
}

struct empty {
    constexpr friend std::strong_ordering operator<=>(empty,
                                                      empty) = default;
};

struct empty_equality {
    friend constexpr bool operator==(const empty_equality &,
                                     const empty_equality &) = default;
};

struct empty_member {
    bool operator==(const empty_member &) const = default;
};

struct padding_only {
    int : 3;
    auto operator<=>(const padding_only &) const = default;
};

enum tag { low = 1, high = 2 };
const unsigned extra = 4;

enum kind : unsigned short { both = low | high, all = both | extra };
enum kind : unsigned short;

static_assert((empty{} <=> empty{}) == 0);
static_assert(empty_equality{} == empty_equality{});

int main()
{
    empty a, b;
    bool equal = (a <=> b) == std::strong_ordering::equal;
    bool less = a < b;
    if (!equal || less)
        return 1;
    empty_equality c, d;
    bool same = c == d, differ = c != d;
    if (!same || differ)
        return 2;
    empty_member e, f;
    same = e == f;
    differ = e != f;
    if (!same || differ)
        return 3;
    padding_only g, h;
    same = g == h;
    differ = (g <=> h) != 0;
    if (!same || differ)
        return 4;
    unsigned value = all;
    if (value != 7)
        return 5;
    return 0;
}
