// [class.access.base] Example 2 (paragraph 4), docs/cxx-spec/class/class.access.base.md
// needs: P5
// status: well-formed (as far as the example says)

class B {
public:
  int m;
};

class S: private B {
  friend class N;
};

class N: private S {
  void f() {
    B* p = this;    // OK because class S satisfies the fourth condition above: B is a base class of N
                    // accessible in f() because B is an accessible base class of S and S is an accessible
                    // base class of N.
  }
};
