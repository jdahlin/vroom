// [expr.new] Example 3, part 2 (paragraph 4), docs/cxx-spec/expr/expr.new.md
// needs: P6 (*[10] begins a lambda-expression until then)
// status: annotated
void example() {
(new int) (*[10])();            // error // syntax-error
}
