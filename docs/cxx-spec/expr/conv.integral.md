# 7.3.8 Integral conversions [conv.integral]

[Contents](../README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.7 Floating-point promotion \[conv.fpprom\]](conv.fpprom.md) · next: [7.3.9 Floating-point conversions \[conv.double\]](conv.double.md)

**1** A prvalue of an integer type can be converted to a prvalue of another integer type. A prvalue of an unscoped enumeration type can be converted to a prvalue of an integer type.

**2** If the destination type is `bool`, see [[conv.bool]](conv.bool.md). If the source type is `bool`, the value `false` is converted to zero and the value `true` is converted to one.

**3** Otherwise, the result is the unique value of the destination type that is congruent to the source integer modulo 2<sup>*N*</sup>, where *N* is the width of the destination type.

**4** The conversions allowed as integral promotions are excluded from the set of integral conversions.
