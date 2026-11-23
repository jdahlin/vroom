// [expr.prim.lambda.capture] Example 3 (paragraph 6), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: annotated

int x = 4;
auto y = [&r = x, x = x+1]()->int {
            r += 2;
            return x+2;
         }();                               // Updates ::x to 6, and initializes y to 7.

auto z = [a = 42](int a) { return 1; };     // error: parameter and local variable have the same name // semantic
