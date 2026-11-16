# 26.8.6.7 Regular modified cylindrical Bessel functions [sf.cmath.cyl.bessel.i]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.6 Complete elliptic integral of the third kind \[sf.cmath.comp.ellint.3\]](sf.cmath.comp.ellint.3.md) · next: [26.8.6.8 Cylindrical Bessel functions of the first kind \[sf.cmath.cyl.bessel.j\]](sf.cmath.cyl.bessel.j.md)

```cpp
double       cyl_bessel_i(double nu, double x);
float        cyl_bessel_if(float nu, float x);
long double  cyl_bessel_il(long double nu, long double x);
```

**1** *Effects:* These functions compute the regular modified cylindrical Bessel functions of their respective arguments `nu` and `x`.

**2** *Returns:*

$$\mathsf{I}_\nu(x) =
     \mathrm{i}^{-\nu} \mathsf{J}_\nu(\mathrm{i}x) =
     \sum_{k=0}^\infty \frac{(x/2)^{\nu+2k}}{k! \: \Gamma(\nu+k+1)}
     \text{ ,\quad for $x \ge 0$,}$$

where ν is `nu` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `nu >= 128`.

**4** See also [[sf.cmath.cyl.bessel.j]](sf.cmath.cyl.bessel.j.md).
