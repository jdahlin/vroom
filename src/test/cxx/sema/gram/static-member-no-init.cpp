/* [temp.static]/1: a static data member of a class template defined outside
   it without an initializer is a definition ([basic.def]/2) */
template <class T> struct A {
  static int n;
  static A<T> instance;
};
template <class T> int A<T>::n;
template <class T> A<T> A<T>::instance;
