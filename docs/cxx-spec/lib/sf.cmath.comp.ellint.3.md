# 26.8.6.6 Complete elliptic integral of the third kind [sf.cmath.comp.ellint.3]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.5 Complete elliptic integral of the second kind \[sf.cmath.comp.ellint.2\]](sf.cmath.comp.ellint.2.md) · next: [26.8.6.7 Regular modified cylindrical Bessel functions \[sf.cmath.cyl.bessel.i\]](sf.cmath.cyl.bessel.i.md)

```cpp
double       comp_ellint_3(double k, double nu);
float        comp_ellint_3f(float k, float nu);
long double  comp_ellint_3l(long double k, long double nu);
```

**1** *Effects:* These functions compute the complete elliptic integral of the third kind of their respective arguments `k` and `nu`.

**2** *Returns:*

$$\mathsf{\Pi}(\nu, k) = \mathsf{\Pi}(\nu, k, \pi / 2) \text{ ,\quad for $|k| \le 1$,}$$

where *k* is `k` and ν is `nu`.

**3** See also [[sf.cmath.ellint.3]](sf.cmath.ellint.3.md).
