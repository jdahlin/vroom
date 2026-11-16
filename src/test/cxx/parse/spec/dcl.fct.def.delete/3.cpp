// [dcl.fct.def.delete] Example 3 (paragraph 3), docs/cxx-spec/dcl/dcl.fct.def.delete.md
// needs: P5
// status: annotated

struct moveonly {
  moveonly() = default;
  moveonly(const moveonly&) = delete;
  moveonly(moveonly&&) = default;
  moveonly& operator=(const moveonly&) = delete;
  moveonly& operator=(moveonly&&) = default;
  ~moveonly() = default;
};
moveonly* p;
moveonly q(*p);                 // error: deleted copy constructor // semantic
