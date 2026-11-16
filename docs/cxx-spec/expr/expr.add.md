# 7.6.6 Additive operators [expr.add]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.5 Multiplicative operators \[expr.mul\]](expr.mul.md) · next: [7.6.7 Shift operators \[expr.shift\]](expr.shift.md)

**1** The additive operators `+` and `-` group left-to-right. The usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed for operands of arithmetic or enumeration type.

```bnf
additive-expression:
    multiplicative-expression
    additive-expression '+' multiplicative-expression
    additive-expression '-' multiplicative-expression
```

For addition, either both operands shall have arithmetic or unscoped enumeration type, or one operand shall be a pointer to a completely-defined object type and the other shall have integral or unscoped enumeration type.

**2** For subtraction, one of the following shall hold:

- both operands have arithmetic or unscoped enumeration type; or
- both operands are pointers to cv-qualified or cv-unqualified versions of the same completely-defined object type; or
- the left operand is a pointer to a completely-defined object type and the right operand has integral or unscoped enumeration type.

**3** The result of the binary `+` operator is the sum of the operands. The result of the binary `-` operator is the difference resulting from the subtraction of the second operand from the first.

**4** When an expression `J` that has integral type is added to or subtracted from an expression `P` of pointer type, the result has the type of `P`.

- If `P` evaluates to a null pointer value and `J` evaluates to 0, the result is a null pointer value.
- Otherwise, if `P` points to an array element *i* of an array object `x` with *n* elements ([[dcl.array]](../dcl/dcl.array.md)),[^1] the expressions `P + J` and `J + P` (where `J` has the value *j*) point to the (possibly-hypothetical) array element *i* + *j* of `x` if 0 ≤ *i* + *j* ≤ *n* and the expression `P - J` points to the (possibly-hypothetical) array element *i* - *j* of `x` if 0 ≤ *i* - *j* ≤ *n*.
- Otherwise, the behavior is undefined.

**5** When two pointer expressions `P` and `Q` are subtracted, the type of the result is an implementation-defined signed integral type; this type shall be the same type that is defined as `std::ptrdiff_t` in the `<cstddef>` header ([[support.types.layout]](../lib/support/support.types.layout.md)).

- If `P` and `Q` both evaluate to null pointer values, the result is 0.
- Otherwise, if `P` and `Q` point to, respectively, array elements *i* and *j* of the same array object `x`, the expression `P - Q` has the value *i* - *j*.
- Otherwise, the behavior is undefined. [*Note 1*: If the value *i* - *j* is not in the range of representable values of type `std::ptrdiff_t`, the behavior is undefined. — *end note*]

**6** For addition or subtraction, if the expressions `P` or `Q` have type “pointer to *cv* `T`”, where `T` and the array element type are not similar ([[conv.qual]](conv.qual.md)), the behavior is undefined. [*Note 2*: In particular, a pointer to a base class cannot be used for pointer arithmetic when the array contains objects of a derived class type. — *end note*]

[^1]: As specified in [[basic.compound]](../basic/basic.compound.md), an object that is not an array element is considered to belong to a single-element array for this purpose and a pointer past the last element of an array of *n* elements is considered to be equivalent to a pointer to a hypothetical array element *n* for this purpose.
