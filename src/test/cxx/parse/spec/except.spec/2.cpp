// [except.spec] Example 2 (paragraph 5), docs/cxx-spec/except/except.spec.md
// needs: P6
// status: well-formed (as far as the example says)

extern void f();                // potentially-throwing

void g() noexcept {
  f();                          // valid, even if f throws
  throw 42;                     // valid, effectively a call to std::terminate
}
