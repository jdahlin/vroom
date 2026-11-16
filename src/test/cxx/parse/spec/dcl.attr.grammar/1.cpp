// [dcl.attr.grammar] Example 1 (paragraph 2), docs/cxx-spec/dcl/dcl.attr.grammar.md
// needs: P4
// status: annotated

[[using CC: opt(1), debug]]         // same as [[CC::opt(1), CC::debug]]
  void f() {}
[[using CC: opt(1)]] [[CC::debug]]  // same as [[CC::opt(1)]] [[CC::debug]]
  void g() {}
[[using CC: CC::opt(1)]]            // error: cannot combine using and scoped attribute token // syntax-error
  void h() {}
