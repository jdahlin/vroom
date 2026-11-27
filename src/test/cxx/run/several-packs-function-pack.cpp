/* [temp.variadic]: a function parameter pack of one of several template
   parameter packs; [expr.prim.lambda.capture]/10: a reference to a
   function captured */
typedef decltype(sizeof 0) size_t;
template <size_t... I> struct seq {};
template <int N> struct V { int v[N]; int operator[](int i) const { return v[i]; } };
template <typename Fn, typename... Args, size_t... I>
inline auto map(seq<I...>, Fn&& fn, const Args&... args) -> V<sizeof...(I)> {
    auto lane = [&](size_t i) { return fn(args[static_cast<int>(i)]...); };
    return { lane(I)... };
}
template <typename Fn, int N, typename... Rest>
auto map(Fn&& fn, const V<N>& first, const Rest&... rest) {
    return map(seq<0, 1, 2>{}, fn, first, rest...);
}
int add(int a, int b) { return a + b; }
int neg(int a) { return -a; }
int main() {
    V<3> a{{1, 2, 3}}, b{{10, 20, 30}};
    V<3> c = map(add, a, b);
    V<3> d = map(neg, a);
    if (c.v[0] != 11 || c.v[2] != 33 || d.v[1] != -2) return 1;
    return 0;
}
