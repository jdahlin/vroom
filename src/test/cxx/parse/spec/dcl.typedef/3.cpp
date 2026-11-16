// [dcl.typedef] Example 3 (paragraph 3), docs/cxx-spec/dcl/dcl.typedef.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

typedef struct s { /* ... */ } s;
typedef int I;
typedef int I;
typedef I I;
