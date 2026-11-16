# 26.8.6.21 Spherical Neumann functions [sf.cmath.sph.neumann]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.20 Spherical associated Legendre functions \[sf.cmath.sph.legendre\]](sf.cmath.sph.legendre.md) · next: [26.9 Numbers \[numbers\]](numbers.md)

```cpp
double       sph_neumann(unsigned n, double x);
float        sph_neumannf(unsigned n, float x);
long double  sph_neumannl(unsigned n, long double x);
```

**1** *Effects:* These functions compute the spherical Neumann functions, also known as the spherical Bessel functions of the second kind, of their respective arguments `n` and `x`.

**2** *Returns:*

$$\mathsf{n}_n(x) = (\pi/2x)^{1\!/\!2} \mathsf{N}_{n + 1\!/\!2}(x)
   \text{ ,\quad for $x \ge 0$,}$$

where *n* is `n` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `n >= 128`.

**4** See also [[sf.cmath.cyl.neumann]](sf.cmath.cyl.neumann.md).
