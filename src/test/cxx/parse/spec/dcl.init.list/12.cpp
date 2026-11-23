// [dcl.init.list] Example 12 (paragraph 5), docs/cxx-spec/dcl/dcl.init.list.md
// needs: P7
// status: well-formed (as far as the example says)

struct X {
  X(std::initializer_list<double> v);
};
X x{ 1,2,3 };
