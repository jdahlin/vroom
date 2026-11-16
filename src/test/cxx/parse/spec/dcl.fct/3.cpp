// [dcl.fct] Example 3 (paragraph 7), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

typedef void F();
struct S {
  const F f;        // OK: equivalent to: void f();
};
