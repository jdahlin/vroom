// [dcl.link] Example 5 (paragraph 8), docs/cxx-spec/dcl/dcl.link.md
// needs: P5
// status: annotated

extern "C" double f();
static double f();                  // error // semantic
extern "C" int i;                   // declaration
extern "C" {
  int i;                            // definition
}
extern "C" static void g();         // error // semantic
