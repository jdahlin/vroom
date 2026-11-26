/* [dcl.constexpr]/1, [depr.static.constexpr]/1 [impl.clang]: a static data
   member a constexpr declaration at namespace scope defines or redeclares is
   emitted whether used or not. */
namespace std {
struct strong_ordering {
    int n;
    constexpr operator int() const { return n; }
    static const strong_ordering less, equal, greater;
};
constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0},
    strong_ordering::greater{1};
}
namespace N {
struct o {
    int n;
    static const o less, equal;
};
constexpr o o::less{-1}, o::equal{0};
}
struct W { static const int a, b; };
constexpr int W::a = 1, W::b = 2;
const int *g() { return &std::strong_ordering::less.n; }
struct A {
    static constexpr int x = 1;
    static constexpr int pair[2] = {2, 3};
};
constexpr int A::x;
constexpr int A::pair[2];
int main() { return *g() + A::x + std::strong_ordering::greater.n + N::o::equal.n; }
