# 7.4 Usual arithmetic conversions [expr.arith.conv]

[Contents](README.md) · up: [7 Expressions \[expr\]](expr.md) · prev: [7.3.14 Boolean conversions \[conv.bool\]](conv.bool.md) · next: [7.5 Primary expressions \[expr.prim\]](expr.prim.md)

**1** Many binary operators that expect operands of arithmetic or enumeration type cause conversions and yield result types in a similar way. The purpose is to yield a common type, which is also the type of the result. This pattern is called the *usual arithmetic conversions*, which are defined as follows:

- If either operand is of scoped enumeration type ([[dcl.enum]](dcl.enum.md)), no conversions are performed; if the other operand does not have the same type, the expression is ill-formed.

- If either operand is of type `long double`, the other shall be converted to `long double`.

- Otherwise, if either operand is `double`, the other shall be converted to `double`.

- Otherwise, if either operand is `float`, the other shall be converted to `float`.

- Otherwise, the integral promotions ([[conv.prom]](conv.prom.md)) shall be performed on both operands.[^1] Then the following rules shall be applied to the promoted operands:

  - If both operands have the same type, no further conversion is needed.
  - Otherwise, if both operands have signed integer types or both have unsigned integer types, the operand with the type of lesser integer conversion rank shall be converted to the type of the operand with greater rank.
  - Otherwise, if the operand that has unsigned integer type has rank greater than or equal to the rank of the type of the other operand, the operand with signed integer type shall be converted to the type of the operand with unsigned integer type.
  - Otherwise, if the type of the operand with signed integer type can represent all of the values of the type of the operand with unsigned integer type, the operand with unsigned integer type shall be converted to the type of the operand with signed integer type.
  - Otherwise, both operands shall be converted to the unsigned integer type corresponding to the type of the operand with signed integer type.

**2** If one operand is of enumeration type and the other operand is of a different enumeration type or a floating-point type, this behavior is deprecated ([[depr.arith.conv.enum]](depr.arith.conv.enum.md)).

[^1]: As a consequence, operands of type `bool`, `char8_t`, `char16_t`, `char32_t`, `wchar_t`, or an enumerated type are converted to some integral type.
