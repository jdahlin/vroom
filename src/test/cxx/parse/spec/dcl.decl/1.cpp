// [dcl.decl] Example 1 (paragraph 4), docs/cxx-spec/dcl/dcl.decl.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

void f1(int a) requires true;               // error: non-templated function
template<typename T>
  auto f2(T a) -> bool requires true;       // OK
template<typename T>
  auto f3(T a) requires true -> bool;       // error: requires-clause precedes trailing-return-type
void (*pf)() requires true;                 // error: constraint on a variable
void g(int (*)() requires true);            // error: constraint on a parameter-declaration

auto* p = new void(*)(char) requires true;  // error: not a function declaration
