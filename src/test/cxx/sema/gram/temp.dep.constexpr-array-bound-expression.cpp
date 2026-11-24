// [temp.dep.constexpr]/2: an array bound that is an expression of a
// non-type template-parameter is value-dependent; the declarator is
// analysed again with the argument ([temp.deduct.type]/5.3: the bound is a
// non-deduced context).
struct B {};
template <int N> auto get(B) -> int (&)[N + 1];
template <int N> int (&get2(B))[N * 2];
template <int N> void take(int (&)[N + 1]);
int (&r)[3] = get<2>(B());
int (&r2)[4] = get2<2>(B());
int (&r3)[6] = get2<3>(B());
