// [stmt.ranged] Example 1 (paragraph 1), docs/cxx-spec/stmt/stmt.ranged.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
int array[5] = { 1, 2, 3, 4, 5 };
for (int& x : array)
  x *= 2;
}
