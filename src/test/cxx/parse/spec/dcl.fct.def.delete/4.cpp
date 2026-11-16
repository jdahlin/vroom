// [dcl.fct.def.delete] Example 4 (paragraph 4), docs/cxx-spec/dcl/dcl.fct.def.delete.md
// needs: P5
// status: annotated

struct sometype {
  sometype();
};
sometype::sometype() = delete;  // error: not first declaration // semantic
