// [expr.spaceship]/4-7: the value of <=> on arithmetic operands, in a constant expression and at run time
// std: c++20
namespace std {
struct strong_ordering {
    signed char v;
    static const strong_ordering less, equal, equivalent, greater;
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, strong_ordering::equivalent{0},
    strong_ordering::greater{1};
struct partial_ordering {
    signed char v;
    static const partial_ordering less, equivalent, greater, unordered;
};
inline constexpr partial_ordering partial_ordering::less{-1}, partial_ordering::equivalent{0},
    partial_ordering::greater{1}, partial_ordering::unordered{-127};
}
static_assert((1 <=> 2).v == -1);
static_assert((2 <=> 2).v == 0);
static_assert((3u <=> 2u).v == 1);
static_assert((1.0 <=> 2.0).v == -1);
static_assert((2.0f <=> 2.0f).v == 0);
static_assert((2.5 <=> 1).v == 1);
constexpr std::strong_ordering o = 5 <=> 3;
static_assert(o.v == 1);
int order(int a, int b) { return (a <=> b).v; }
int order(double a, double b) { return (a <=> b).v; }
int calls;
int next() { return ++calls; }
int main() {
    if (order(1, 2) != -1 || order(2, 2) != 0 || order(5, 2) != 1) return 1;
    if (order(1.0, 2.0) != -1 || order(2.0, 2.0) != 0 || order(5.0, 2.0) != 1) return 2;
    volatile double zero = 0.0;
    double n = zero / zero;
    if (order(n, 1.0) != -127 || order(1.0, n) != -127) return 3;
    int c = (next() <=> next()).v;
    if (calls != 2 || c != -1) return 4;
    std::strong_ordering s = 3 <=> 3;
    if (s.v != 0) return 5;
    return 0;
}
