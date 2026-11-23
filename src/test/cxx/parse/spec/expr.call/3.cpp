// [expr.call] Example 3 (paragraph 8), docs/cxx-spec/expr/expr.call.md
// needs: P4
// status: well-formed (as far as the example says)

void f() {
  std::string s = "but I have heard it works even if you don't believe in it";
  s.replace(0, 4, "").replace(s.find("even"), 4, "only").replace(s.find(" don't"), 6, "");
  assert(s == "I have heard it works only if you believe in it");       // OK
}
