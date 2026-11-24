// [temp.deduct]/8: access checking is done as part of the substitution, in
// the template's context; a friend function template of the class may name
// its private member type in its declaration ([temp.friend]/3).
template <class T> class A {
  typedef int type;
  template <class U> friend void foo(U &, typename U::type);
};
template <class U> void foo(U &, typename U::type) {}
void test() {
  A<int> a;
  foo(a, 0);
}
