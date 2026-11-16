# 7.6.11 Bitwise AND operator [expr.bit.and]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.10 Equality operators \[expr.eq\]](expr.eq.md) · next: [7.6.12 Bitwise exclusive OR operator \[expr.xor\]](expr.xor.md)

```bnf
and-expression:
    equality-expression
    and-expression '&' equality-expression
```

**1** The `&` operator groups left-to-right. The operands shall be of integral or unscoped enumeration type. The usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed. Given the coefficients `x`<sub>*i*</sub> and `y`<sub>*i*</sub> of the base-2 representation ([[basic.fundamental]](basic.fundamental.md)) of the converted operands `x` and `y`, the coefficient `r`<sub>*i*</sub> of the base-2 representation of the result `r` is 1 if both `x`<sub>*i*</sub> and `y`<sub>*i*</sub> are 1, and 0 otherwise. [*Note 1*: The result is the bitwise AND function of the operands. — *end note*]
