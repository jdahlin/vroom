# D.2 Implicit capture of `*this` by reference [depr.capture.this]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.1 Arithmetic conversion on enumerations \[depr.arith.conv.enum\]](depr.arith.conv.enum.md) · next: [D.3 Comma operator in subscript expressions \[depr.comma.subscript\]](depr.comma.subscript.md)

**1** For compatibility with prior C++ International Standards, a *lambda-expression* with *capture-default* `=` ([[expr.prim.lambda.capture]](../expr/expr.prim.lambda.capture.md)) may implicitly capture `*this` by reference. [*Example 1*:

```cpp
struct X {
  int x;
  void foo(int n) {
    auto f = [=]() { x = n; };          // deprecated: x means this->x, not a copy thereof
    auto g = [=, this]() { x = n; };    // recommended replacement
  }
};
```

— *end example*]
