# 7.6.16 Conditional operator [expr.cond]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.15 Logical OR operator \[expr.log.or\]](expr.log.or.md) · next: [7.6.17 Yielding a value \[expr.yield\]](expr.yield.md)

```bnf
conditional-expression:
    logical-or-expression
    logical-or-expression '?' expression ':' assignment-expression
```

**1** Conditional expressions group right-to-left. The first expression is contextually converted to `bool` ([[conv]](conv.md)). It is evaluated and if it is `true`, the result of the conditional expression is the value of the second expression, otherwise that of the third expression. Only one of the second and third expressions is evaluated. The first expression is sequenced before the second or third expression ([[intro.execution]](intro.execution.md)).

**2** If either the second or the third operand has type `void`, one of the following shall hold:

- The second or the third operand (but not both) is a (possibly parenthesized) *throw-expression* ([[expr.throw]](expr.throw.md)); the result is of the type and value category of the other. The *conditional-expression* is a bit-field if that operand is a bit-field.
- Both the second and the third operands have type `void`; the result is of type `void` and is a prvalue. [*Note 1*: This includes the case where both operands are *throw-expression*s. — *end note*]

**3** Otherwise, if the second and third operand are glvalue bit-fields of the same value category and of types *cv1* `T` and *cv2* `T`, respectively, the operands are considered to be of type *cv* `T` for the remainder of this subclause, where *cv* is the union of *cv1* and *cv2*.

**4** Otherwise, if the second and third operand have different types and either has (possibly cv-qualified) class type, or if both are glvalues of the same value category and the same type except for cv-qualification, an attempt is made to form an implicit conversion sequence ([[over.best.ics]](over.best.ics.md)) from each of those operands to the type of the other. [*Note 2*: Properties such as access, whether an operand is a bit-field, or whether a conversion function is deleted are ignored for that determination. — *end note*] Attempts are made to form an implicit conversion sequence from an operand expression `E1` of type `T1` to a target type related to the type `T2` of the operand expression `E2` as follows:

- If `E2` is an lvalue, the target type is “lvalue reference to `T2`”, subject to the constraint that in the conversion the reference must bind directly ([[dcl.init.ref]](dcl.init.ref.md)) to a glvalue.

- If `E2` is an xvalue, the target type is “rvalue reference to `T2`”, subject to the constraint that the reference must bind directly.

- If `E2` is a prvalue or if neither of the conversion sequences above can be formed and at least one of the operands has (possibly cv-qualified) class type:

  - if `T1` and `T2` are the same class type (ignoring cv-qualification) and `T2` is at least as cv-qualified as `T1`, the target type is `T2`,
  - otherwise, if `T2` is a base class of `T1`, the target type is *cv1* `T2`, where *cv1* denotes the cv-qualifiers of `T1`,
  - otherwise, the target type is the type that `E2` would have after applying the lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions.

Using this process, it is determined whether an implicit conversion sequence can be formed from the second operand to the target type determined for the third operand, and vice versa. If both sequences can be formed, or one can be formed but it is the ambiguous conversion sequence, the program is ill-formed. If no conversion sequence can be formed, the operands are left unchanged and further checking is performed as described below. Otherwise, if exactly one conversion sequence can be formed, that conversion is applied to the chosen operand and the converted operand is used in place of the original operand for the remainder of this subclause. [*Note 3*: The conversion might be ill-formed even if an implicit conversion sequence could be formed. — *end note*]

**5** If the second and third operands are glvalues of the same value category and have the same type, the result is of that type and value category and it is a bit-field if the second or the third operand is a bit-field, or if both are bit-fields.

**6** Otherwise, the result is a prvalue. If the second and third operands do not have the same type, and either has (possibly cv-qualified) class type, overload resolution is used to determine the conversions (if any) to be applied to the operands ([[over.match.oper]](over.match.oper.md), [[over.built]](over.built.md)). If the overload resolution fails, the program is ill-formed. Otherwise, the conversions thus determined are applied, and the converted operands are used in place of the original operands for the remainder of this subclause.

**7** Lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are performed on the second and third operands. After those conversions, one of the following shall hold:

- The second and third operands have the same type; the result is of that type and the result object is initialized using the selected operand.
- The second and third operands have arithmetic or enumeration type; the usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed to bring them to a common type, and the result is of that type.
- One or both of the second and third operands have pointer type; pointer conversions ([[conv.ptr]](conv.ptr.md)), function pointer conversions ([[conv.fctptr]](conv.fctptr.md)), and qualification conversions ([[conv.qual]](conv.qual.md)) are performed to bring them to their composite pointer type ([[expr.type]](expr.type.md)). The result is of the composite pointer type.
- One or both of the second and third operands have pointer-to-member type; pointer to member conversions ([[conv.mem]](conv.mem.md)), function pointer conversions ([[conv.fctptr]](conv.fctptr.md)), and qualification conversions ([[conv.qual]](conv.qual.md)) are performed to bring them to their composite pointer type ([[expr.type]](expr.type.md)). The result is of the composite pointer type.
- Both the second and third operands have type `std::nullptr_t` or one has that type and the other is a null pointer constant. The result is of type `std::nullptr_t`.
