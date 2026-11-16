// [except.throw] Example 2 (paragraph 7), docs/cxx-spec/except/except.throw.md
// needs: P6
// status: well-formed (as far as the example says)

struct C {
  C() { }
  C(const C&) {
    if (std::uncaught_exceptions()) {
      throw 0;      // throw during copy to handler's exception-declaration object ([except.handle])
    }
  }
};

int main() {
  try {
    throw C();      // calls std::terminate if construction of the handler's
                    // exception-declaration object is not elided ([class.copy.elision])
  } catch(C) { }
}
