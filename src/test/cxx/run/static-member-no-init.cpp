// [temp.static]/1: a static data member of a class template defined outside
// it without an initializer: a definition, zero-initialized ([basic.start.static]/2)
template <class T> struct A {
  static T n;
  static A<T> instance;
  T v;
};
template <class T> T A<T>::n;
template <class T> A<T> A<T>::instance;
int main() {
  if (A<int>::n != 0 || A<int>::instance.v != 0)
    return 1;
  A<int>::n = 5;
  A<long>::instance.v = 7;
  if (A<int>::n != 5 || A<long>::instance.v != 7)
    return 2;
  return 0;
}
