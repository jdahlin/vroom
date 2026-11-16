// [stmt.iter] Example 1, part 2 (paragraph 2), docs/cxx-spec/stmt/stmt.iter.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
while (--x >= 0) {
  int i;
}
}
