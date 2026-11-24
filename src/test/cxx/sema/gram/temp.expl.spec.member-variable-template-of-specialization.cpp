// [temp.expl.spec]/16: a member variable template of an implicit instantiation,
// defined for that specialization alone.
template<class T> struct A { template<int> static const int u; };
template<> template<int> const int A<int>::u = 3;
static_assert(A<int>::u<0> == 3, "");
