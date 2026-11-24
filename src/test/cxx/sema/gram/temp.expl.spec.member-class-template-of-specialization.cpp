// [temp.expl.spec]/16: a member class template of an implicit instantiation,
// defined for that specialization alone.
template <class T> struct esm_A {
  template <class X> struct B;
};
template <> template <class X> struct esm_A<int>::B {
  X x;
  int i;
};
esm_A<int>::B<char> esm_v;
static_assert(sizeof(esm_A<int>::B<long>) == 16, "");
