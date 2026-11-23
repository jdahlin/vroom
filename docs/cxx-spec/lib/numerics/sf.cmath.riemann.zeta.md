# 26.8.6.18 Riemann zeta function [sf.cmath.riemann.zeta]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.17 Legendre polynomials \[sf.cmath.legendre\]](sf.cmath.legendre.md) · next: [26.8.6.19 Spherical Bessel functions of the first kind \[sf.cmath.sph.bessel\]](sf.cmath.sph.bessel.md)

```cpp
double       riemann_zeta(double x);
float        riemann_zetaf(float x);
long double  riemann_zetal(long double x);
```

**1** *Effects:* These functions compute the Riemann zeta function of their respective arguments `x`.

**2** *Returns:*

$$\mathsf{\zeta}(x) =
  \left\{
  \begin{array}{cl}
  \displaystyle
  \sum_{k=1}^\infty k^{-x},
  & \mbox{for $x > 1$}
  \\
  \\
  \displaystyle
  \frac{1}
	{1 - 2^{1-x}}
  \sum_{k=1}^\infty (-1)^{k-1} k^{-x},
  & \mbox{for $0 \le x \le 1$}
  \\
  \\
  \displaystyle
  2^x \pi^{x-1} \sin(\frac{\pi x}{2}) \, \Gamma(1-x) \, \zeta(1-x),
  & \mbox{for $x < 0$}
  \end{array}
  \right.
\;$$

where *x* is `x`.
