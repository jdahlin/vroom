# 7.6.15 Logical OR operator [expr.log.or]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.14 Logical AND operator \[expr.log.and\]](expr.log.and.md) · next: [7.6.16 Conditional operator \[expr.cond\]](expr.cond.md)

```bnf
logical-or-expression:
    logical-and-expression
    logical-or-expression '||' logical-and-expression
```

**1** The `||` operator groups left-to-right. The operands are both contextually converted to `bool` ([[conv]](conv.md)). The result is `true` if either of its operands is `true`, and `false` otherwise. Unlike `|`, `||` guarantees left-to-right evaluation; moreover, the second operand is not evaluated if the first operand evaluates to `true`.

**2** The result is a `bool`. If the second expression is evaluated, the first expression is sequenced before the second expression ([[intro.execution]](../basic/intro.execution.md)).
