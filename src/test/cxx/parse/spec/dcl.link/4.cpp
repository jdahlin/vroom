// [dcl.link] Example 4 (paragraph 7), docs/cxx-spec/dcl/dcl.link.md
// needs: P5
// status: annotated

int x;
namespace A {
  extern "C" int f();
  extern "C" int g() { return 1; }
  extern "C" int h();
  extern "C" int x();               // error: same name as global-space object x // semantic
}

namespace B {
  extern "C" int f();               // A::f and B::f refer to the same function
  extern "C" int g() { return 1; }  // error: the function g with C language linkage has two definitions // semantic
}

int A::f() { return 98; }           // definition for the function f with C language linkage
extern "C" int h() { return 97; }   // definition for the function h with C language linkage
                                    // A::h and ::h refer to the same function
