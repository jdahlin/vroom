# 7.3.10 Floating-integral conversions [conv.fpint]

[Contents](../README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.9 Floating-point conversions \[conv.double\]](conv.double.md) · next: [7.3.11 Pointer conversions \[conv.ptr\]](conv.ptr.md)

**1** A prvalue of a floating-point type can be converted to a prvalue of an integer type. The conversion truncates; that is, the fractional part is discarded. The behavior is undefined if the truncated value cannot be represented in the destination type. [*Note 1*: If the destination type is `bool`, see [[conv.bool]](conv.bool.md). — *end note*]

**2** A prvalue of an integer type or of an unscoped enumeration type can be converted to a prvalue of a floating-point type. The result is exact if possible. If the value being converted is in the range of values that can be represented but the value cannot be represented exactly, it is an implementation-defined choice of either the next lower or higher representable value. [*Note 2*: Loss of precision occurs if the integral value cannot be represented exactly as a value of the floating-point type. — *end note*] If the value being converted is outside the range of values that can be represented, the behavior is undefined. If the source type is `bool`, the value `false` is converted to zero and the value `true` is converted to one.
