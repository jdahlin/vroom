# 26.8.6.20 Spherical associated Legendre functions [sf.cmath.sph.legendre]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.19 Spherical Bessel functions of the first kind \[sf.cmath.sph.bessel\]](sf.cmath.sph.bessel.md) · next: [26.8.6.21 Spherical Neumann functions \[sf.cmath.sph.neumann\]](sf.cmath.sph.neumann.md)

```cpp
double       sph_legendre(unsigned l, unsigned m, double theta);
float        sph_legendref(unsigned l, unsigned m, float theta);
long double  sph_legendrel(unsigned l, unsigned m, long double theta);
```

**1** *Effects:* These functions compute the spherical associated Legendre functions of their respective arguments `l`, `m`, and `theta` (`theta` measured in radians).

**2** *Returns:*

$$\mathsf{Y}_\ell^m(\theta, 0)$$

where

$$\mathsf{Y}_\ell^m(\theta, \phi) =
     (-1)^m \left[\frac{(2 \ell + 1)}{4 \pi} \frac{(\ell - m)!}{(\ell + m)!}\right]^{1/2}
     \mathsf{P}_\ell^m (\cos\theta) e^{i m \phi}
     \text{ ,\quad for $|m| \le \ell$,}$$

and *l* is `l`, *m* is `m`, and θ is `theta`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `l >= 128`.

**4** See also [[sf.cmath.assoc.legendre]](sf.cmath.assoc.legendre.md).
