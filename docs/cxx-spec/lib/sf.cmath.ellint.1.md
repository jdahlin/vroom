# 26.8.6.11 Incomplete elliptic integral of the first kind [sf.cmath.ellint.1]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.10 Cylindrical Neumann functions \[sf.cmath.cyl.neumann\]](sf.cmath.cyl.neumann.md) · next: [26.8.6.12 Incomplete elliptic integral of the second kind \[sf.cmath.ellint.2\]](sf.cmath.ellint.2.md)

```cpp
double       ellint_1(double k, double phi);
float        ellint_1f(float k, float phi);
long double  ellint_1l(long double k, long double phi);
```

**1** *Effects:* These functions compute the incomplete elliptic integral of the first kind of their respective arguments `k` and `phi` (`phi` measured in radians).

**2** *Returns:*

$$\mathsf{F}(k, \phi) =
     \int_0^\phi \! \frac{\mathsf{d}\theta}{\sqrt{1 - k^2 \sin^2 \theta}}
     \text{ ,\quad for $|k| \le 1$,}$$

where *k* is `k` and φ is `phi`.
