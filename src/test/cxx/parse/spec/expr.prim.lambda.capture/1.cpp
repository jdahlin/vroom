// [expr.prim.lambda.capture] Example 1 (paragraph 2), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: annotated

struct S2 { void f(int i); };
void S2::f(int i) {
  [&, i]{ };        // OK
  [&, this, i]{ };  // OK, equivalent to [&, i]
  [&, &i]{ };       // error: i preceded by & when & is the default // semantic
  [=, *this]{ };    // OK
  [=, this]{ };     // OK, equivalent to [=]
  [i, i]{ };        // error: i repeated // semantic
  [this, *this]{ }; // error: this appears twice // semantic
}
