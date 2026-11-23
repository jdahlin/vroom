// [dcl.ptr] Example 1, part 2 (paragraph 2), docs/cxx-spec/dcl/dcl.ptr.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
i = ci;
*cp = ci;
pc++;
pc = cpc;
pc = p;
ppc = &pc;
}
