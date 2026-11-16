// [dcl.ptr] Example 1, part 4 (paragraph 2), docs/cxx-spec/dcl/dcl.ptr.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

*ppc = &ci;         // OK, but would make p point to ci because of previous error
*p = 5;             // clobber ci
