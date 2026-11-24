// [temp.mem]/1: a member class template of a class template, defined outside
// it; each specialization's member template takes the definition.
template <class T> struct mco_S {
  template <class U> struct X;
  template <class U> struct Y { T t; U u; };
};
template <class T> template <class U> struct mco_S<T>::X {
  T t;
  U u;
  int f() { return sizeof(T) + sizeof(U); }
};
mco_S<int>::X<char> mco_v;
static_assert(sizeof(mco_S<int>::X<long>) == 16, "");
static_assert(sizeof(mco_S<char>::X<char>) == 2, "");
