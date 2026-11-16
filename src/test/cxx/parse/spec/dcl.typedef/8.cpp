// [dcl.typedef] Example 8 (paragraph 8), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: annotated

struct S {
  S();
  ~S();
};

typedef struct S T;

S a = T();                      // OK
struct T * p;                   // error // semantic
