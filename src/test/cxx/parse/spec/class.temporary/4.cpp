// [class.temporary] Example 4 (paragraph 6), docs/cxx-spec/basic/class.temporary.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  const int& m;
};
const S& s = S{1};              // both S and int temporaries have lifetime of s
