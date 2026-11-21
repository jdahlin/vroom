// In-class initializer on a static constexpr floating-point data member
// ([dcl.constexpr]/1, [class.static.data]/4).

template <typename T>
struct Grower {
    static constexpr double kExactFit = 1.0;
    static constexpr double kGrowing = 1.5;
    static constexpr float kHalf = 0.5f;

    static double scale(double x) { return x * kGrowing + kExactFit + kHalf; }
};

static_assert(Grower<int>::kExactFit == 1.0);
static_assert(Grower<int>::kGrowing == 1.5);
static_assert(Grower<int>::kHalf == 0.5f);

int main() {
    double r = Grower<int>::scale(2.0);
    return (r > 4.49 && r < 4.51) ? 0 : 1;
}
