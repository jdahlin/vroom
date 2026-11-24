// [temp.expl.spec]/2 (CWG727): an explicit specialization of a member template in the class's own scope
struct S {
  template <class T> void f();
  template <> void f<int>();
  template <> void f<char>() {}
};
template <> void S::f<int>() {}
void use(S &s) { s.f<int>(); s.f<char>(); }
