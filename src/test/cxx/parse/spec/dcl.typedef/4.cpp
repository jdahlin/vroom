// [dcl.typedef] Example 4 (paragraph 4), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: annotated

struct S {
  typedef struct A { } A;       // OK
  typedef struct B B;           // OK
  typedef A A;                  // error // semantic
};
