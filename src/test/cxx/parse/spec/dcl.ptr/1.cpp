// [dcl.ptr] Example 1 (paragraph 2), docs/cxx-spec/dcl/dcl.ptr.md
// needs: P5
// status: well-formed (as far as the example says)

const int ci = 10, *pc = &ci, *const cpc = pc, **ppc;
int i, *p, *const cp = &i;
