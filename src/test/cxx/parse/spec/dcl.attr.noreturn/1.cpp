// [dcl.attr.noreturn] Example 1 (paragraph 4), docs/cxx-spec/dcl/dcl.attr.noreturn.md
// needs: P4
// status: annotated

[[ noreturn ]] void f() {
  throw "error";                // OK
}

[[ noreturn ]] void q(int i) {  // behavior is undefined if called with an argument <= 0
  if (i > 0)
    throw "positive";
}
