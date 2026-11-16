# 7.6.7 Shift operators [expr.shift]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.6 Additive operators \[expr.add\]](expr.add.md) · next: [7.6.8 Three-way comparison operator \[expr.spaceship\]](expr.spaceship.md)

**1** The shift operators `<<` and `>>` group left-to-right.

```bnf
shift-expression:
    additive-expression
    shift-expression '<<' additive-expression
    shift-expression '>>' additive-expression
```

The operands shall be of integral or unscoped enumeration type and integral promotions are performed. The type of the result is that of the promoted left operand. The behavior is undefined if the right operand is negative, or greater than or equal to the width of the promoted left operand.

**2** The value of `E1 << E2` is the unique value congruent to `E1` × 2<sup></sup>*E*2 modulo 2<sup>*N*</sup>, where *N* is the width of the type of the result. [*Note 1*: `E1` is left-shifted `E2` bit positions; vacated bits are zero-filled. — *end note*]

**3** The value of `E1 >> E2` is `E1` / 2<sup></sup>*E*2, rounded down. [*Note 2*: `E1` is right-shifted `E2` bit positions. Right-shift on signed integral types is an arithmetic right shift, which performs sign-extension. — *end note*]

**4** The expression `E1` is sequenced before the expression `E2`.
