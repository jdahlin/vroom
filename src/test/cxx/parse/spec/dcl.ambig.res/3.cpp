// [dcl.ambig.res] Example 3 (paragraph 3), docs/cxx-spec/dcl/dcl.ambig.res.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

class C { };
void f(int(C)) { }              // void f(int(*fp)(C c)) { }
                                // not: void f(int C) { }

int g(C);

void foo() {
  f(1);                         // error: cannot convert 1 to function pointer
  f(g);                         // OK
}
