// [dcl.ref] Example 2 (paragraph 3), docs/cxx-spec/dcl/dcl.ref.md
// needs: P4
// status: well-formed (as far as the example says)

void f(double& a) { a += 3.14; }
// ...
void example() {
double d = 0;
f(d);
}
