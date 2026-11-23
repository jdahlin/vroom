// [expr.mptr.oper] Example 1 (paragraph 6), docs/cxx-spec/expr/expr.mptr.oper.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
(ptr_to_obj->*ptr_to_mfct)(10);
}
