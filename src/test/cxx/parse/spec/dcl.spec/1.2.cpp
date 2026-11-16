// [dcl.spec] Example 1, part 2 (paragraph 3), docs/cxx-spec/dcl/dcl.spec.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

void f(const Pc);               // void f(char* const) (not const char*)
void g(const int Pc);           // void g(const int)
