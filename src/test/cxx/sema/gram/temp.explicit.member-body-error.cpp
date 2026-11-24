// [temp.explicit]/10: an explicit instantiation definition instantiates the members defined at that point; one ill-formed for these arguments makes the explicit instantiation ill-formed
template <class T, class R> struct C {
  void f(T t) { R r = t; }
};
template struct C<int, long>;
template struct C<int, char *>; // expected-error@3 {{cannot initialize}} expected-note {{in instantiation}}
