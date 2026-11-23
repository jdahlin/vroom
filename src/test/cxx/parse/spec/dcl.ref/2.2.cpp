// [dcl.ref] Example 2, part 2 (paragraph 3), docs/cxx-spec/dcl/dcl.ref.md
// needs: P4
// status: well-formed (as far as the example says)

int v[20];
// ...
int& g(int i) { return v[i]; }
// ...
void example() {
g(3) = 7;
}
