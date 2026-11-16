// [dcl.constexpr] Example 4 (paragraph 6), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

constexpr int f(bool b)
  { return b ? throw 0 : 0; }           // OK
constexpr int f() { return f(true); }   // ill-formed, no diagnostic required

struct B {
  constexpr B(int x) : i(0) { }         // x is unused
  int i;
};

int global;

struct D : B {
  constexpr D() : B(global) { }         // ill-formed, no diagnostic required
                                        // lvalue-to-rvalue conversion on non-constant global
};
