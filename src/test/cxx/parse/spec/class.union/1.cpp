// [class.union] Example 1 (paragraph 5), docs/cxx-spec/class/class.union.md
// needs: P7 (std::string is not declared)
// status: well-formed (as far as the example says)

union U {
  int i;
  float f;
  std::string s;
};
