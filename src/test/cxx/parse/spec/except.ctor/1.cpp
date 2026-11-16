// [except.ctor] Example 1 (paragraph 2), docs/cxx-spec/except/except.ctor.md
// needs: P6
// status: well-formed (as far as the example says)

struct A { };

struct Y { ~Y() noexcept(false) { throw 0; } };

A f() {
  try {
    A a;
    Y y;
    A b;
    return {};      // #1
  } catch (...) {
  }
  return {};        // #2
}
