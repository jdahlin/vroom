# 26.8.6.4 Complete elliptic integral of the first kind [sf.cmath.comp.ellint.1]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.3 Beta function \[sf.cmath.beta\]](sf.cmath.beta.md) · next: [26.8.6.5 Complete elliptic integral of the second kind \[sf.cmath.comp.ellint.2\]](sf.cmath.comp.ellint.2.md)

```cpp
double       comp_ellint_1(double k);
float        comp_ellint_1f(float k);
long double  comp_ellint_1l(long double k);
```

**1** *Effects:* These functions compute the complete elliptic integral of the first kind of their respective arguments `k`.

**2** *Returns:*

$$\mathsf{K}(k) = \mathsf{F}(k, \pi / 2) \text{ ,\quad for $|k| \le 1$,}$$

where *k* is `k`.

**3** See also [[sf.cmath.ellint.1]](sf.cmath.ellint.1.md).
