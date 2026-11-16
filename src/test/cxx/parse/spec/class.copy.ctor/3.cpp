// [class.copy.ctor] Example 3 (paragraph 3), docs/cxx-spec/class/class.copy.ctor.md
// needs: P5
// status: well-formed (as far as the example says)

struct X {
  X(const X&);
  X(X&);            // OK
  X(X&&);
  X(const X&&);     // OK, but possibly not sensible
};
