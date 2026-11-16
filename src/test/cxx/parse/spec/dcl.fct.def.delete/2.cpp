// [dcl.fct.def.delete] Example 2 (paragraph 3), docs/cxx-spec/dcl/dcl.fct.def.delete.md
// needs: P5
// status: annotated

struct sometype {
  void* operator new(std::size_t) = delete;
  void* operator new[](std::size_t) = delete;
};
sometype* p = new sometype;     // error: deleted class operator new // semantic
sometype* q = new sometype[3];  // error: deleted class operator new[] // semantic
