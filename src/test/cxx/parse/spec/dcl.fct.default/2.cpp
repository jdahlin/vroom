// [dcl.fct.default] Example 2 (paragraph 4), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P7
// status: annotated

void g(int = 0, ...);           // OK, ellipsis is not a parameter so it can follow
                                // a parameter with a default argument
void f(int, int);
void f(int, int = 7);
void h() {
  f(3);                         // OK, calls f(3, 7)
  void f(int = 1, int);         // error: does not use default from surrounding scope // semantic
}
void m() {
  void f(int, int);             // has no defaults
  f(4);                         // error: wrong number of arguments // semantic
  void f(int, int = 5);         // OK
  f(4);                         // OK, calls f(4, 5);
  void f(int, int = 5);         // error: cannot redefine, even to same value // semantic
}
void n() {
  f(6);                         // OK, calls f(6, 7)
}
template<class ... T> struct C {
  void f(int n = 0, T...);
};
C<int> c;                       // OK, instantiates declaration void C::f(int n = 0, int)
