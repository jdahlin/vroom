// [class.temporary] Example 6 (paragraph 8), docs/cxx-spec/basic/class.temporary.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  S();
  S(int);
  friend S operator+(const S&, const S&);
  ~S();
};
S obj1;
const S& cr = S(16)+S(23);
S obj2;
