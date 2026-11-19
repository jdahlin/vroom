/* cxx-run-flags: -std=c++20 */
/* [class.compare.default]/1, [class.eq]/3: a defaulted friend operator==
   compares the members in order; unnamed parameters by value (libc++'s
   partial_ordering, after a GNU attribute) and named ones of type const C& */
enum class result : signed char { less = -1, equal = 0 };
struct ordering {
    result value;
    __attribute__((__visibility__("hidden"))) friend constexpr bool
    operator==(ordering, ordering) noexcept = default;
};
struct point {
    int x;
    long y;
    friend bool operator==(const point &a, const point &b) = default;
};
int main()
{
    ordering a{result::less}, b{result::less}, c{result::equal};
    point p{1, 2}, q{1, 2}, r{1, 3};
    return !(a == b) + (a == c) + !(p == q) + (p == r);
}
