// [dcl.link] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.link.md
// needs: P5 (names declared in another part or outside the example stay unparsed)
// status: well-formed (as far as the example says)

complex sqrt(complex);          // C++ linkage by default
extern "C" {
  double sqrt(double);          // C linkage
}
