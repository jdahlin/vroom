// [dcl.stc] Example 3 (paragraph 8), docs/cxx-spec/dcl/dcl.stc.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

class X {
  mutable const int* p;         // OK
  mutable int* const q;         // error
};
