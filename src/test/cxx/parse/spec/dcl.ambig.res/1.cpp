// [dcl.ambig.res] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.ambig.res.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

struct S {
  S(int);
};

void foo(double a) {
  S w(int(a));                  // function declaration
  S x(int());                   // function declaration
  S y((int(a)));                // object declaration
  S y((int)a);                  // object declaration
  S z = int(a);                 // object declaration
}
