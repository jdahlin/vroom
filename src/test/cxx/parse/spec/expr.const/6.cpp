// [expr.const] Example 6 (paragraph 12), docs/cxx-spec/expr/expr.const.md
// needs: P4
// status: well-formed (as far as the example says)

bool f() {
    char array[1 + int(1 + 0.2 - 0.1 - 0.1)];   // Must be evaluated during translation
    int size = 1 + int(1 + 0.2 - 0.1 - 0.1);    // May be evaluated at runtime
    return sizeof(array) == size;
}
