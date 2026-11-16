# D.1 Arithmetic conversion on enumerations [depr.arith.conv.enum]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [Annex D Compatibility features \[depr\]](depr.md) · next: [D.2 Implicit capture of `*this` by reference \[depr.capture.this\]](depr.capture.this.md)

**1** The ability to apply the usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) on operands where one is of enumeration type and the other is of a different enumeration type or a floating-point type is deprecated. [*Note 1*: Three-way comparisons ([[expr.spaceship]](expr.spaceship.md)) between such operands are ill-formed. — *end note*] [*Example 1*:

```cpp
enum E1 { e };
enum E2 { f };
bool b = e <= 3.7;              // deprecated
int k = f - e;                  // deprecated
auto cmp = e <=> f;             // error
```

— *end example*]
