// [dcl.link] Example 3 (paragraph 5), docs/cxx-spec/dcl/dcl.link.md
// needs: P5
// status: well-formed (as far as the example says)

extern "C" typedef void FUNC_c();

class C {
  void mf1(FUNC_c*);            // the name of the function mf1 and the member function's type have
                                // C++ language linkage; the parameter has type ``pointer to C function''

  FUNC_c mf2;                   // the name of the function mf2 and the member function's type have
                                // C++ language linkage

  static FUNC_c* q;             // the name of the data member q has C++ language linkage and
                                // the data member's type is ``pointer to C function''
};

extern "C" {
  class X {
    void mf();                  // the name of the function mf and the member function's type have
                                // C++ language linkage
    void mf2(void(*)());        // the name of the function mf2 has C++ language linkage;
                                // the parameter has type ``pointer to C function''
  };
}
