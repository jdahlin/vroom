// [dcl.stc] Example 3 (paragraph 8), docs/cxx-spec/dcl/dcl.stc.md
// needs: P5
// status: annotated

class X {
  mutable const int* p;         // OK
  mutable int* const q;         // error // semantic
};
