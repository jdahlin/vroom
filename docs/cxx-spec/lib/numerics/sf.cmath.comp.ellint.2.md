# 26.8.6.5 Complete elliptic integral of the second kind [sf.cmath.comp.ellint.2]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.4 Complete elliptic integral of the first kind \[sf.cmath.comp.ellint.1\]](sf.cmath.comp.ellint.1.md) · next: [26.8.6.6 Complete elliptic integral of the third kind \[sf.cmath.comp.ellint.3\]](sf.cmath.comp.ellint.3.md)

```cpp
double       comp_ellint_2(double k);
float        comp_ellint_2f(float k);
long double  comp_ellint_2l(long double k);
```

**1** *Effects:* These functions compute the complete elliptic integral of the second kind of their respective arguments `k`.

**2** *Returns:*

$$\mathsf{E}(k) = \mathsf{E}(k, \pi / 2) \text{ ,\quad for $|k| \le 1$,}$$

where *k* is `k`.

**3** See also [[sf.cmath.ellint.2]](sf.cmath.ellint.2.md).
