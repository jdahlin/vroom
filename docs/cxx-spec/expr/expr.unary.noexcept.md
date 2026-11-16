# 7.6.2.6 `noexcept` operator [expr.unary.noexcept]

[Contents](../README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.5 Alignof \[expr.alignof\]](expr.alignof.md) · next: [7.6.2.7 New \[expr.new\]](expr.new.md)

**1** The `noexcept` operator determines whether the evaluation of its operand, which is an unevaluated operand ([[expr.prop]](expr.prop.md)), can throw an exception ([[except.throw]](../except/except.throw.md)).

```bnf
noexcept-expression:
  'noexcept' '(' expression ')'
```

**2** The result of the `noexcept` operator is a prvalue of type `bool`. [*Note 1*: A *noexcept-expression* is an integral constant expression ([[expr.const]](expr.const.md)). — *end note*]

**3** The result of the `noexcept` operator is `true` unless the *expression* is potentially-throwing ([[except.spec]](../except/except.spec.md)).
