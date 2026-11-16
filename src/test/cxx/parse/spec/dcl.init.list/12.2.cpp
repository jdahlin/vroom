// [dcl.init.list] Example 12, part 2 (paragraph 5), docs/cxx-spec/dcl/dcl.init.list.md
// needs: P4
// status: well-formed (as far as the example says)

const double __a[3] = {double{1}, double{2}, double{3}};
X x(std::initializer_list<double>(__a, __a+3));
