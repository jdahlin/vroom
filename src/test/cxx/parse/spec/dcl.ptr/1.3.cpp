// [dcl.ptr] Example 1, part 3 (paragraph 2), docs/cxx-spec/dcl/dcl.ptr.md
// needs: P4
// status: annotated
void example() {
ci = 1;             // error // semantic
ci++;               // error // semantic
*pc = 2;            // error // semantic
cp = &ci;           // error // semantic
cpc++;              // error // semantic
p = pc;             // error // semantic
ppc = &p;           // error // semantic
}
