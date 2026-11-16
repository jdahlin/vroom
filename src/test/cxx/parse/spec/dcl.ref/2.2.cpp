// [dcl.ref] Example 2, part 2 (paragraph 3), docs/cxx-spec/dcl/dcl.ref.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

int v[20];
// ...
int& g(int i) { return v[i]; }
// ...
g(3) = 7;
