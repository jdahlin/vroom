# 7.6.14 Logical AND operator [expr.log.and]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.13 Bitwise inclusive OR operator \[expr.or\]](expr.or.md) · next: [7.6.15 Logical OR operator \[expr.log.or\]](expr.log.or.md)

```bnf
logical-and-expression:
    inclusive-or-expression
    logical-and-expression '&&' inclusive-or-expression
```

**1** The `&&` operator groups left-to-right. The operands are both contextually converted to `bool` ([[conv]](conv.md)). The result is `true` if both operands are `true` and `false` otherwise. Unlike `&`, `&&` guarantees left-to-right evaluation: the second operand is not evaluated if the first operand is `false`.

**2** The result is a `bool`. If the second expression is evaluated, the first expression is sequenced before the second expression ([[intro.execution]](../basic/intro.execution.md)).
