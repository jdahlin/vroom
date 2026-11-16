// [expr.prim.lambda.closure] Example 6 (paragraph 10), docs/cxx-spec/expr/expr.prim.lambda.closure.md
// needs: P6
// status: well-formed (as far as the example says)

auto GL = [](auto a) { std::cout << a; return a; };
int (*GL_int)(int) = GL;        // OK: through conversion function template
GL_int(3);                      // OK: same as GL(3)
