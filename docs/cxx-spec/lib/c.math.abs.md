# 26.8.2 Absolute values [c.math.abs]

[Contents](../README.md) · up: [26.8 Mathematical functions for floating-point types \[c.math\]](c.math.md) · prev: [26.8.1 Header `<cmath>` synopsis \[cmath.syn\]](cmath.syn.md) · next: [26.8.3 Three-dimensional hypotenuse \[c.math.hypot3\]](c.math.hypot3.md)

**1** [*Note 1*: The headers `<cstdlib>` ([[cstdlib.syn]](cstdlib.syn.md)) and `<cmath>` ([[cmath.syn]](cmath.syn.md)) declare the functions described in this subclause. — *end note*]

```cpp
int abs(int j);
long int abs(long int j);
long long int abs(long long int j);
float abs(float j);
double abs(double j);
long double abs(long double j);
```

**2** *Effects:* The `abs` functions have the semantics specified in the C standard library for the functions `abs`, `labs`, `llabs`, `fabsf`, `fabs`, and `fabsl`.

**3** *Remarks:* If `abs()` is called with an argument of type `X` for which `is_unsigned_v<X>` is `true` and if `X` cannot be converted to `int` by integral promotion ([[conv.prom]](../conv.prom.md)), the program is ill-formed. [*Note 2*: Arguments that can be promoted to `int` are permitted for compatibility with C. — *end note*]

*See also:* ISO C 7.12.7.2, 7.22.6.1
