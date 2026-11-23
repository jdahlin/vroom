// [expr.prim.lambda.closure] Example 5 (paragraph 9), docs/cxx-spec/expr/expr.prim.lambda.closure.md
// needs: P6
// status: annotated

void f1(int (*)(int))   { }
void f2(char (*)(int))  { }

void g(int (*)(int))    { }     // #1
void g(char (*)(char))  { }     // #2

void h(int (*)(int))    { }     // #3
void h(char (*)(int))   { }     // #4

auto glambda = [](auto a) { return a; };
f1(glambda);                    // OK
f2(glambda);                    // error: ID is not convertible // semantic
g(glambda);                     // error: ambiguous // semantic
h(glambda);                     // OK: calls #3 since it is convertible from ID
int& (*fpi)(int*) = [](auto* a) -> auto& { return *a; };        // OK
