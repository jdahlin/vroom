// [expr.prim.lambda] Example 1 (paragraph 1), docs/cxx-spec/expr/expr.prim.lambda.md
// needs: P6
// status: well-formed (as far as the example says)

#include <algorithm>
#include <cmath>
void abssort(float* x, unsigned N) {
  std::sort(x, x + N, [](float a, float b) { return std::abs(a) < std::abs(b); });
}
