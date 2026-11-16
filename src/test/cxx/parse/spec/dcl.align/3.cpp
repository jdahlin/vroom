// [dcl.align] Example 3 (paragraph 7), docs/cxx-spec/dcl/dcl.align.md
// needs: P3
// status: annotated

alignas(T) alignas(A) T buffer[N];
