/* cxx-run-flags: -std=c++20 */
/* [class.compare.default]/1, [class.eq]/3: a defaulted member operator==
   (const C&, const-qualified) compares the members in order; its members
   of class type compare with their own ==; it is constexpr ([class.compare.default]/3) */
struct point {
    int x;
    long y;
    bool operator==(const point &) const = default;
};
struct segment {
    point from;
    const char *name;
    bool operator==(const segment &) const = default;
};
constexpr point p0{1, 2}, p1{1, 2}, p2{1, 3};
static_assert(p0 == p1);
static_assert(!(p0 == p2));
int main()
{
    point p{1, 2}, q{1, 2}, r{1, 3};
    segment a{p, "a"}, b{q, a.name}, c{r, a.name};
    return !(p == q) + (p == r) + !(a == b) + (a == c);
}
