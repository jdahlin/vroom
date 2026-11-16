// [class.mfct.non-static] Example 2 (paragraph 4), docs/cxx-spec/class/class.mfct.non-static.md
// needs: P5
// status: well-formed (as far as the example says)

struct X {
  void g() const;
  void h() const volatile;
};
