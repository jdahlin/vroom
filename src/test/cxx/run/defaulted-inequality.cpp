/* cxx-run-flags: -std=c++20 */
/* [class.compare.secondary]/2: a defaulted != is !(x == y) through the
   class's ==, member or friend; constexpr like ==. */
struct point {
    int x;
    long y;
    bool operator==(const point &) const = default;
    bool operator!=(const point &) const = default;
};
struct tag {
    int v;
    friend bool operator==(const tag &, const tag &) = default;
    friend bool operator!=(const tag &, const tag &) = default;
};
constexpr point p0{1, 2}, p1{1, 3};
static_assert(p0 != p1);
static_assert(!(p0 != p0));
int main()
{
    point p{1, 2}, q{1, 2}, r{2, 2};
    tag a{1}, b{1}, c{2};
    return (p != q) + !(p != r) + (a != b) + !(a != c);
}
