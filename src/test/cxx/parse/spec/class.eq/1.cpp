// [class.eq] Example 1 (paragraph 4), docs/cxx-spec/class/class.eq.md
// needs: P5
// status: well-formed (as far as the example says)

struct D {
  int i;
  friend bool operator==(const D& x, const D& y) = default;
                                                // OK, returns x.i == y.i
};
