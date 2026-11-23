// [class.name] Example 1, part 2 (paragraph 1), docs/cxx-spec/class/class.name.md
// needs: P5
// status: annotated
void example() {
a1 = a2;                        // error: Y assigned to X // semantic
a1 = a3;                        // error: int assigned to X // semantic
}
