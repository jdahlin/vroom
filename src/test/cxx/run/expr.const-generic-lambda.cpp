/* [expr.prim.lambda.closure]/4: a generic lambda's specialization is
   constexpr (libc++'s tuple comparisons, which Skia's bentleyottmann
   Segment needs in a static data member initializer) */
template <int... I> struct seq {};
template <int... I> constexpr int fold(seq<I...>, const int (&a)[2], const int (&b)[2]) {
    int r = 0;
    [&]<int... J>(seq<J...>) { ((r = r * 10 + a[J] - b[J]), ...); }(seq<I...>());
    return r;
}
constexpr int sum() { const int a[2] = {3, 5}, b[2] = {1, 2}; return fold(seq<0, 1>(), a, b); }
struct S { static constexpr int k = sum(); };
static_assert(S::k == 23);
int main() { return S::k != 23; }
