// [temp.expl.spec]/16: a declaration of a member template of a class template specialization
template <class T> struct A { template <class U> void f(U); };
template <> template <class U> void A<int>::f(U);
template <> template <class U> void A<int>::f(U) {}
void g() { A<int> a; a.f(1); }
