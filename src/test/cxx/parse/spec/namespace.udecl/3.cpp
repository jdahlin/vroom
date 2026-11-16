// [namespace.udecl] Example 3 (paragraph 3), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P7
// status: annotated

template <typename... bases>
struct X : bases... {
  using bases::g...;
};

X<B, D> x;                      // OK: B::g and D::g introduced
