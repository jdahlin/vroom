# 7.6.13 Bitwise inclusive OR operator [expr.or]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.12 Bitwise exclusive OR operator \[expr.xor\]](expr.xor.md) · next: [7.6.14 Logical AND operator \[expr.log.and\]](expr.log.and.md)

```bnf
inclusive-or-expression:
    exclusive-or-expression
    inclusive-or-expression '|' exclusive-or-expression
```

**1** The `|` operator groups left-to-right. The operands shall be of integral or unscoped enumeration type. The usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed. Given the coefficients `x`<sub>*i*</sub> and `y`<sub>*i*</sub> of the base-2 representation ([[basic.fundamental]](../basic/basic.fundamental.md)) of the converted operands `x` and `y`, the coefficient `r`<sub>*i*</sub> of the base-2 representation of the result `r` is 1 if at least one of `x`<sub>*i*</sub> and `y`<sub>*i*</sub> are 1, and 0 otherwise. [*Note 1*: The result is the bitwise inclusive OR function of the operands. — *end note*]
