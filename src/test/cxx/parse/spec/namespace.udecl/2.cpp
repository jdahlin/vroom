// [namespace.udecl] Example 2 (paragraph 3), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

enum class button { up, down };
struct S {
  using button::up;
  button b = up;                // OK
};
