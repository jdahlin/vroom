// [dcl.ambig.res] Example 3, part 2 (paragraph 3), docs/cxx-spec/dcl/dcl.ambig.res.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

class C { };
void h(int *(C[10]));           // void h(int *(*_fp)(C _parm[10]));
                                // not: void h(int *C[10]);
