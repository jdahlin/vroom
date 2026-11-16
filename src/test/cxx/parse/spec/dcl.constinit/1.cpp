// [dcl.constinit] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.constinit.md
// needs: P5
// status: annotated

const char * g() { return "dynamic initialization"; }
constexpr const char * f(bool p) { return p ? "constant initializer" : g(); }
constinit const char * c = f(true);     // OK
constinit const char * d = f(false);    // error // semantic
