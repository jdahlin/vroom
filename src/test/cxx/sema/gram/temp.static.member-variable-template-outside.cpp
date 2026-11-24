// [temp.static]/1: a static data member template of a class template, defined
// outside the class template; each specialization's member takes the definition.
template <class T> struct msv_A {
  template <int> static const int u;
};
template <class T> template <int N> const int msv_A<T>::u = N + sizeof(T);
static_assert(msv_A<long>::u<5> == 13, "");
static_assert(msv_A<char>::u<5> == 6, "");
