// [dcl.fct] Example 6 (paragraph 16), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

int i,
    *pi,
    f(),
    *fpi(int),
    (*pif)(const char*, const char*),
    (*fpif(int))(int);
