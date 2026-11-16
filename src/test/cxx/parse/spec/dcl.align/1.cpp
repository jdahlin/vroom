// [dcl.align] Example 1 (paragraph 5), docs/cxx-spec/dcl/dcl.align.md
// needs: P5
// status: annotated

struct alignas(8) S {};
struct alignas(1) U {
  S s;
};  // error: U specifies an alignment that is less strict than if the alignas(1) were omitted. // semantic
