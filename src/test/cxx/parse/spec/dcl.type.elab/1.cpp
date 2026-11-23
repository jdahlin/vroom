// [dcl.type.elab] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.type.elab.md
// needs: P5
// status: annotated

enum class E { a, b };
enum E x = E::a;                // OK
struct S { } s;
class S* p = &s;                // OK
