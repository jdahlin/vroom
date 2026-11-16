# 26.8.6.12 Incomplete elliptic integral of the second kind [sf.cmath.ellint.2]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.11 Incomplete elliptic integral of the first kind \[sf.cmath.ellint.1\]](sf.cmath.ellint.1.md) · next: [26.8.6.13 Incomplete elliptic integral of the third kind \[sf.cmath.ellint.3\]](sf.cmath.ellint.3.md)

```cpp
double       ellint_2(double k, double phi);
float        ellint_2f(float k, float phi);
long double  ellint_2l(long double k, long double phi);
```

**1** *Effects:* These functions compute the incomplete elliptic integral of the second kind of their respective arguments `k` and `phi` (`phi` measured in radians).

**2** *Returns:*

$$\mathsf{E}(k, \phi) = \int_0^\phi \! \sqrt{1 - k^2 \sin^2 \theta} \, \mathsf{d}\theta
   \text{ ,\quad for $|k| \le 1$,}$$

where *k* is `k` and φ is `phi`.
