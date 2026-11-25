/* cxx-run-flags: -std=c++17 */
// [abi:mangle.template-arg] a pointer to member as a template argument: X ad L _Z <encoding of the
// member> E E, a member function with its parameter types; a null one L <type> 0 E (clang's
// CodeGenCXX/mangle.cpp test4, test6, mangle-template.cpp test6)
struct A {
  int f(float) { return 1; }
  int d = 2;
  virtual int v() { return 4; }
};
template <int (A::*m)(float)> struct T {
  int call(A &a) { return a.f(0); }
};
template <int A::*m> struct U {
  int get(A &a) { return a.d; }
};
template <int (A::*m)()> struct V {
  int call(A &a) { return a.v(); }
};
int f0(A &a, T<&A::f> t) { return t.call(a); }
int f1(T<nullptr>) { return 0; }
int f2(A &a, U<&A::d> u) { return u.get(a); }
int f3(U<nullptr>) { return 0; }
int f4(A &a, V<&A::v> v) { return v.call(a); }
int main() {
  A a;
  return f0(a, {}) + f1({}) + f2(a, {}) + f3({}) + f4(a, {}) == 7 ? 0 : 1;
}
