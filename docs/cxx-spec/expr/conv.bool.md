# 7.3.14 Boolean conversions [conv.bool]

[Contents](../README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.13 Function pointer conversions \[conv.fctptr\]](conv.fctptr.md) · next: [7.4 Usual arithmetic conversions \[expr.arith.conv\]](expr.arith.conv.md)

**1** A prvalue of arithmetic, unscoped enumeration, pointer, or pointer-to-member type can be converted to a prvalue of type `bool`. A zero value, null pointer value, or null member pointer value is converted to `false`; any other value is converted to `true`.
