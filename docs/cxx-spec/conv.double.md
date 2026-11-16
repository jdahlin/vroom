# 7.3.9 Floating-point conversions [conv.double]

[Contents](README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.8 Integral conversions \[conv.integral\]](conv.integral.md) · next: [7.3.10 Floating-integral conversions \[conv.fpint\]](conv.fpint.md)

**1** A prvalue of floating-point type can be converted to a prvalue of another floating-point type. If the source value can be exactly represented in the destination type, the result of the conversion is that exact representation. If the source value is between two adjacent destination values, the result of the conversion is an implementation-defined choice of either of those values. Otherwise, the behavior is undefined.

**2** The conversions allowed as floating-point promotions are excluded from the set of floating-point conversions.
