// [expr.new] Example 3, part 2 (paragraph 4), docs/cxx-spec/expr/expr.new.md
// needs: P4
// status: annotated

(new int) (*[10])();            // error // syntax-error
