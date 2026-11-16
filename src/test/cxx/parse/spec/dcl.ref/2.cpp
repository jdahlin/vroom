// [dcl.ref] Example 2 (paragraph 3), docs/cxx-spec/dcl/dcl.ref.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

void f(double& a) { a += 3.14; }
// ...
double d = 0;
f(d);
