# 26.8.6.13 Incomplete elliptic integral of the third kind [sf.cmath.ellint.3]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.12 Incomplete elliptic integral of the second kind \[sf.cmath.ellint.2\]](sf.cmath.ellint.2.md) · next: [26.8.6.14 Exponential integral \[sf.cmath.expint\]](sf.cmath.expint.md)

```cpp
double       ellint_3(double k, double nu, double phi);
float        ellint_3f(float k, float nu, float phi);
long double  ellint_3l(long double k, long double nu, long double phi);
```

**1** *Effects:* These functions compute the incomplete elliptic integral of the third kind of their respective arguments `k`, `nu`, and `phi` (`phi` measured in radians).

**2** *Returns:*

$$\mathsf{\Pi}(\nu, k, \phi) = \int_0^\phi \!
   \frac{ \mathsf{d}\theta }{ (1 - \nu \, \sin^2 \theta) \sqrt{1 - k^2 \sin^2 \theta} } \text{ ,\quad for $|k| \le 1$,}$$

where ν is `nu`, *k* is `k`, and φ is `phi`.
