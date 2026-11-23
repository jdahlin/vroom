// [dcl.decl] Example 1 (paragraph 4), docs/cxx-spec/dcl/dcl.decl.md
// needs: P7
// status: annotated

void f1(int a) requires true;               // error: non-templated function // semantic
template<typename T>
  auto f2(T a) -> bool requires true;       // OK
template<typename T>
  auto f3(T a) requires true -> bool;       // error: requires-clause precedes trailing-return-type // syntax-error
void (*pf)() requires true;                 // error: constraint on a variable // syntax-error
void g(int (*)() requires true);            // error: constraint on a parameter-declaration // syntax-error

auto* p = new void(*)(char) requires true;  // error: not a function declaration // syntax-error
