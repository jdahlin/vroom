// [dcl.constexpr] Example 3 (paragraph 4), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

struct Length {
  constexpr explicit Length(int i = 0) : val(i) { }
private:
  int val;
};
