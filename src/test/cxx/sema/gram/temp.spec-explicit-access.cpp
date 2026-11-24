// [temp.spec]/6: access is not checked for the names of an explicit
// specialization's or instantiation's declaration itself (template
// arguments, declarator), but is in its initializer and function body.
class A {
  typedef int P;
  static const int v = 1;
};
template <class T> struct S {};
template <> struct S<A::P> {};
template <class T> struct U {};
template struct U<A::P>;
template <class T> T vt = 0;
template <> A::P vt<A::P> = A::v;
template <class T> void g(T);
template <> void g<A::P>(A::P) { (void)A::v; }
template <class T> void h(T) {}
template void h<A::P>(A::P);
