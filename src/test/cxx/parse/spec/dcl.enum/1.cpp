// [dcl.enum] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.enum.md
// needs: P5
// status: annotated

struct S {
  enum E : int {};
  enum E : int {};              // error: redeclaration of enumeration // semantic
};
