// [dcl.constexpr] Example 3 (paragraph 4), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: P5
// status: well-formed (as far as the example says)

struct Length {
  constexpr explicit Length(int i = 0) : val(i) { }
private:
  int val;
};
