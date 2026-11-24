// [temp.expl.spec]/17: an explicit specialization of a member class of a class template specialization
template <class T> struct S0 { struct S1; struct S2; };
template <> struct S0<int>::S1;
template <> struct S0<char>::S2 { int m; };
template <> struct S0<int>::S1 { int q; };
S0<int>::S1 a;
S0<char>::S2 b;
int f() { return a.q + b.m; }
