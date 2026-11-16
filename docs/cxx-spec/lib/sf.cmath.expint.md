# 26.8.6.14 Exponential integral [sf.cmath.expint]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.13 Incomplete elliptic integral of the third kind \[sf.cmath.ellint.3\]](sf.cmath.ellint.3.md) · next: [26.8.6.15 Hermite polynomials \[sf.cmath.hermite\]](sf.cmath.hermite.md)

```cpp
double       expint(double x);
float        expintf(float x);
long double  expintl(long double x);
```

**1** *Effects:* These functions compute the exponential integral of their respective arguments `x`.

**2** *Returns:*

$$\mathsf{Ei}(x) =
  - \int_{-x}^\infty \frac{e^{-t}}
                          {t     } \, \mathsf{d}t
\;$$

where *x* is `x`.
