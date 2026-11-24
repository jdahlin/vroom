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
/* [class.eq]/3: the base classes first, then the members, an array's
   elements one by one */
struct line : point {
    int steps[3];
    int grid[2][2];
    bool operator==(const line &) const = default;
};
struct tagged : point {
    int tag;
    friend bool operator==(const tagged &, const tagged &) = default;
};
constexpr line l0{{1, 2}, {1, 2, 3}, {{1, 2}, {3, 4}}},
    l1{{1, 2}, {1, 2, 3}, {{1, 2}, {3, 4}}},
    l2{{1, 2}, {1, 2, 3}, {{1, 2}, {3, 5}}};
static_assert(l0 == l1);
static_assert(!(l0 == l2));
constexpr point p0{1, 2}, p1{1, 2}, p2{1, 3};
static_assert(p0 == p1);
static_assert(!(p0 == p2));
int main()
{
    point p{1, 2}, q{1, 2}, r{1, 3};
    segment a{p, "a"}, b{q, a.name}, c{r, a.name};
    tagged t{{1, 2}, 3}, u{{1, 2}, 3}, v{{1, 3}, 3};
    return !(t == u) + (t == v) + !(p == q) + (p == r) + !(a == b) + (a == c);
}
