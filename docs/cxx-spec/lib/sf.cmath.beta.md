# 26.8.6.3 Beta function [sf.cmath.beta]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.2 Associated Legendre functions \[sf.cmath.assoc.legendre\]](sf.cmath.assoc.legendre.md) · next: [26.8.6.4 Complete elliptic integral of the first kind \[sf.cmath.comp.ellint.1\]](sf.cmath.comp.ellint.1.md)

```cpp
double       beta(double x, double y);
float        betaf(float x, float y);
long double  betal(long double x, long double y);
```

**1** *Effects:* These functions compute the beta function of their respective arguments `x` and `y`.

**2** *Returns:*

$$\mathsf{B}(x, y) = \frac{\Gamma(x) \, \Gamma(y)}{\Gamma(x + y)}
   \text{ ,\quad for $x > 0$,\, $y > 0$,}$$

where *x* is `x` and *y* is `y`.
