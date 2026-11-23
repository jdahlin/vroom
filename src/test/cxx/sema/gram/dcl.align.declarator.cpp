// [dcl.align]/1, [dcl.decl]/4: an alignment-specifier after the
// declarator-id appertains to the variable or data member; not to a typedef.
alignas(1) int n5 alignas(4);
int n6 alignas(8) = 1;
struct S {
  int a alignas(16);
};
static_assert(alignof(S) == 16, "");
typedef int A alignas(4); // expected-error {{'alignas' attribute only applies to variables, data members and tag types}}
