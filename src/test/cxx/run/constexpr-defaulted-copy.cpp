// [dcl.fct.def.default]/3: constexpr defaulted copy and move constructors
// and assignment operators.
struct P {
    int x;
    constexpr P(int v) : x(v) {}
    constexpr P(const P &) = default;
    constexpr P(P &&) = default;
    constexpr P &operator=(const P &) = default;
    constexpr P &operator=(P &&) = default;
};
constexpr P copy(P p) { P q = p; return q; }
static_assert(copy(P(4)).x == 4, "");

int main() {
    P a(1), b(2);
    a = b;
    return copy(a).x != 2;
}
