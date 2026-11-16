// [dcl.typedef] Example 3 (paragraph 3), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: well-formed (as far as the example says)

typedef struct s { /* ... */ } s;
typedef int I;
typedef int I;
typedef I I;
