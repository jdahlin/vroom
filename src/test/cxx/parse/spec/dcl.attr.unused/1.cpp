// [dcl.attr.unused] Example 1 (paragraph 5), docs/cxx-spec/dcl/dcl.attr.unused.md
// needs: P4
// status: annotated

[[maybe_unused]] void f([[maybe_unused]] bool thing1,
                        [[maybe_unused]] bool thing2) {
  [[maybe_unused]] bool b = thing1 && thing2;
  assert(b);
}
