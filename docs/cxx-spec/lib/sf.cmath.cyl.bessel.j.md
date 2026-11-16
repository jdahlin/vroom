# 26.8.6.8 Cylindrical Bessel functions of the first kind [sf.cmath.cyl.bessel.j]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.7 Regular modified cylindrical Bessel functions \[sf.cmath.cyl.bessel.i\]](sf.cmath.cyl.bessel.i.md) · next: [26.8.6.9 Irregular modified cylindrical Bessel functions \[sf.cmath.cyl.bessel.k\]](sf.cmath.cyl.bessel.k.md)

```cpp
double       cyl_bessel_j(double nu, double x);
float        cyl_bessel_jf(float nu, float x);
long double  cyl_bessel_jl(long double nu, long double x);
```

**1** *Effects:* These functions compute the cylindrical Bessel functions of the first kind of their respective arguments `nu` and `x`.

**2** *Returns:*

$$\mathsf{J}_\nu(x) =
   \sum_{k=0}^\infty \frac{(-1)^k (x/2)^{\nu+2k}}{k! \: \Gamma(\nu+k+1)}
   \text{ ,\quad for $x \ge 0$,}$$

where ν is `nu` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `nu >= 128`.
