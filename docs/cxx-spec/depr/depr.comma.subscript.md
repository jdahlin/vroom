# D.3 Comma operator in subscript expressions [depr.comma.subscript]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.2 Implicit capture of `*this` by reference \[depr.capture.this\]](depr.capture.this.md) · next: [D.4 Array comparisons \[depr.array.comp\]](depr.array.comp.md)

**1** A comma expression ([[expr.comma]](../expr/expr.comma.md)) appearing as the *expr-or-braced-init-list* of a subscripting expression ([[expr.sub]](../expr/expr.sub.md)) is deprecated. [*Note 1*: A parenthesized comma expression is not deprecated. — *end note*] [*Example 1*:

```cpp
void f(int *a, int b, int c) {
    a[b,c];                     // deprecated
    a[(b,c)];                   // OK
}
```

— *end example*]
