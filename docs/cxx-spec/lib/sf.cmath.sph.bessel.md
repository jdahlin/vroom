# 26.8.6.19 Spherical Bessel functions of the first kind [sf.cmath.sph.bessel]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.18 Riemann zeta function \[sf.cmath.riemann.zeta\]](sf.cmath.riemann.zeta.md) · next: [26.8.6.20 Spherical associated Legendre functions \[sf.cmath.sph.legendre\]](sf.cmath.sph.legendre.md)

```cpp
double       sph_bessel(unsigned n, double x);
float        sph_besself(unsigned n, float x);
long double  sph_bessell(unsigned n, long double x);
```

**1** *Effects:* These functions compute the spherical Bessel functions of the first kind of their respective arguments `n` and `x`.

**2** *Returns:*

$$\mathsf{j}_n(x) = (\pi/2x)^{1\!/\!2} \mathsf{J}_{n + 1\!/\!2}(x) \text{ ,\quad for $x \ge 0$,}$$

where *n* is `n` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `n >= 128`.

**4** See also [[sf.cmath.cyl.bessel.j]](sf.cmath.cyl.bessel.j.md).
