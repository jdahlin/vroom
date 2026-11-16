// [dcl.align] Example 2 (paragraph 6), docs/cxx-spec/dcl/dcl.align.md
// needs: P5
// status: annotated

// Translation unit #1:
struct S { int x; } s, *p = &s;

// Translation unit #2:
struct alignas(16) S;           // ill-formed, no diagnostic required: definition of S lacks alignment // semantic
extern S* p;
