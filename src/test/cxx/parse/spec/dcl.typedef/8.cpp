// [dcl.typedef] Example 8 (paragraph 8), docs/cxx-spec/dcl/dcl.typedef.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

struct S {
  S();
  ~S();
};

typedef struct S T;

S a = T();                      // OK
struct T * p;                   // error
