// [temp.dep.constexpr]/2: a template argument that is an expression of a
// non-type template-parameter (S<I + 1>) in a function template's
// declarator is value-dependent: a non-deduced context
// ([temp.deduct.type]/5.3), compared by its tokens ([temp.over.link]/5),
// evaluated once an instantiation gives I.
template <int N> struct S {};
template<int I> int over(S<I+1>) { return 1; }
template<int I> int over(S<I*2>) { return 2; }
template<int I> int same(S<I+1>);
template<int I> int same(S<I+1>);
template<int I> int same(S<I+1>) { return 3; }
template<int I> int ded(S<I+1>, S<I>) { return I; }
S<3> a; S<6> c; S<2> d;
int x1 = over<2>(a);
int x2 = over<3>(c);
int x3 = same<2>(a);
int x4 = ded<2>(a, d);
