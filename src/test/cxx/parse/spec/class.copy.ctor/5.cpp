// [class.copy.ctor] Example 5 (paragraph 5), docs/cxx-spec/class/class.copy.ctor.md
// needs: P7
// status: well-formed (as far as the example says)

struct S {
  template<typename T> S(T);
  S();
};

S g;

void h() {
  S a(g);           // does not instantiate the member template to produce S::S<S>(S);
                    // uses the implicitly declared copy constructor
}
