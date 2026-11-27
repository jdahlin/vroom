/* [expr.const], [expr.unary.op]/3: pointer-to-member constants in constexpr
   variables and arrays of them (Skia's table of BuiltinTypes members) */
struct B { int a, b, c; int f() const { return a; } int g() const { return c; } };
static constexpr int B::*kData[] = { &B::a, &B::c, nullptr };
static constexpr int (B::*kFun[])() const = { &B::f, &B::g };
static constexpr int B::*kOne = &B::b;
static constexpr int B::*kCopy = kOne;
struct R { int B::*p; int B::*q; };
static constexpr R kR{&B::c, &B::a};
static_assert(kCopy);
static_assert(!kData[2]);
int main()
{
    B v{1, 2, 3};
    static constexpr const int B::*local[] = { &B::b, &B::a };
    constexpr int B::*n{};
    int r = 0;
    for (auto p : kData) { if (p) r = r * 10 + v.*p; else r = r * 10 + 7; }
    if (r != 137) return 1;
    for (auto p : kFun) r = r * 10 + (v.*p)();
    if (r != 13713) return 2;
    if (v.*kOne != 2 || v.*kCopy != 2) return 3;
    if (v.*kR.p != 3 || v.*kR.q != 1) return 4;
    if (v.*local[0] != 2 || v.*local[1] != 1 || n) return 5;
    return 0;
}
