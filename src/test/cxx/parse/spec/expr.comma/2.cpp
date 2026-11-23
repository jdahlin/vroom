// [expr.comma] Example 2 (paragraph 2), docs/cxx-spec/expr/expr.comma.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
f(a, (t=3, t+2), c);
}
