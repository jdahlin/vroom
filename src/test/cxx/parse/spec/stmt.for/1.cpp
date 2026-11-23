// [stmt.for] Example 1 (paragraph 3), docs/cxx-spec/stmt/stmt.for.md
// needs: P4
// status: well-formed (as far as the example says)
void example() {
int i = 42;
int a[10];

for (int i = 0; i < 10; i++)
  a[i] = i;

int j = i;          // j = 42
}
