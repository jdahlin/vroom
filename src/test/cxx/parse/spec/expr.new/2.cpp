// [expr.new] Example 2 (paragraph 3), docs/cxx-spec/expr/expr.new.md
// needs: P4
// status: annotated

new int * i;                    // syntax error: parsed as (new int*) i, not as (new int)*i
