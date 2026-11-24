// [class.friend]/2 and [class.access]/6: the template-head of a friend class
// template is the befriended entity's, so it may name the granting class's
// private members.
class A {
  typedef int I;
  template <int> friend struct B;
  template <template <int> class T> friend struct TT;
};
template <A::I> struct B {};
template <template <A::I> class T> struct TT {};
