// [dcl.ptr] Example 1, part 4 (paragraph 2), docs/cxx-spec/dcl/dcl.ptr.md
// needs: P4
// status: annotated
void example() {
*ppc = &ci;         // OK, but would make p point to ci because of previous error
*p = 5;             // clobber ci
}
