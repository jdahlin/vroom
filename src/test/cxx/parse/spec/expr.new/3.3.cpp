// [expr.new] Example 3, part 3 (paragraph 4), docs/cxx-spec/expr/expr.new.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
new (int (*[10])());
}
