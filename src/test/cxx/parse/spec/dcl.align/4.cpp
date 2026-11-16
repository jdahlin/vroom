// [dcl.align] Example 4 (paragraph 8), docs/cxx-spec/dcl/dcl.align.md
// needs: P3
// status: annotated

alignas(double) void f();                           // error: alignment applied to function // semantic
alignas(double) unsigned char c[sizeof(double)];    // array of characters, suitably aligned for a double
extern unsigned char c[sizeof(double)];             // no alignas necessary
alignas(float)
  extern unsigned char c[sizeof(double)];           // error: different alignment in declaration // semantic
