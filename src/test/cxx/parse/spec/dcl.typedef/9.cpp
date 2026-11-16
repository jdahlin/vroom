// [dcl.typedef] Example 9 (paragraph 9), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P6
// status: well-formed (as far as the example says)

typedef struct { } *ps, S;      // S is the class name for linkage purposes
typedef decltype([]{}) C;       // the closure type has no name for linkage purposes
