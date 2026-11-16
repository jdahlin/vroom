# 7.6.5 Multiplicative operators [expr.mul]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.4 Pointer-to-member operators \[expr.mptr.oper\]](expr.mptr.oper.md) · next: [7.6.6 Additive operators \[expr.add\]](expr.add.md)

**1** The multiplicative operators `*`, `/`, and `%` group left-to-right.

```bnf
multiplicative-expression:
    pm-expression
    multiplicative-expression '*' pm-expression
    multiplicative-expression '/' pm-expression
    multiplicative-expression '%' pm-expression
```

**2** The operands of `*` and `/` shall have arithmetic or unscoped enumeration type; the operands of `%` shall have integral or unscoped enumeration type. The usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed on the operands and determine the type of the result.

**3** The binary `*` operator indicates multiplication.

**4** The binary `/` operator yields the quotient, and the binary `%` operator yields the remainder from the division of the first expression by the second. If the second operand of `/` or `%` is zero the behavior is undefined. For integral operands the `/` operator yields the algebraic quotient with any fractional part discarded;[^1] if the quotient `a/b` is representable in the type of the result, `(a/b)*b + a%b` is equal to `a`; otherwise, the behavior of both `a/b` and `a%b` is undefined.

[^1]: This is often called truncation towards zero.
