// [dcl.mptr] Example 1 (paragraph 2), docs/cxx-spec/dcl/dcl.mptr.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

struct X {
  void f(int);
  int a;
};
struct Y;

int X::* pmi = &X::a;
void (X::* pmf)(int) = &X::f;
double X::* pmd;
char Y::* pmc;
