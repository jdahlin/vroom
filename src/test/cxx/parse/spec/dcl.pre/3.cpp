// [dcl.pre] Example 3 (paragraph 6), docs/cxx-spec/dcl/dcl.pre.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

static_assert(sizeof(int) == sizeof(void*), "wrong pointer size");
