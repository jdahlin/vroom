# 26.8.6.10 Cylindrical Neumann functions [sf.cmath.cyl.neumann]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.9 Irregular modified cylindrical Bessel functions \[sf.cmath.cyl.bessel.k\]](sf.cmath.cyl.bessel.k.md) · next: [26.8.6.11 Incomplete elliptic integral of the first kind \[sf.cmath.ellint.1\]](sf.cmath.ellint.1.md)

```cpp
double       cyl_neumann(double nu, double x);
float        cyl_neumannf(float nu, float x);
long double  cyl_neumannl(long double nu, long double x);
```

**1** *Effects:* These functions compute the cylindrical Neumann functions, also known as the cylindrical Bessel functions of the second kind, of their respective arguments `nu` and `x`.

**2** *Returns:*

$$\mathsf{N}_\nu(x) =
  \left\{
  \begin{array}{cl}
  \displaystyle
  \frac{\mathsf{J}_\nu(x) \cos \nu\pi - \mathsf{J}_{-\nu}(x)}
       {\sin \nu\pi },
  & \mbox{for $x \ge 0$ and non-integral $\nu$}
  \\
  \\
  \displaystyle
  \lim_{\mu \rightarrow \nu} \frac{\mathsf{J}_\mu(x) \cos \mu\pi - \mathsf{J}_{-\mu}(x)}
                                {\sin \mu\pi },
  & \mbox{for $x \ge 0$ and integral $\nu$}
  \end{array}
  \right.$$

where ν is `nu` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `nu >= 128`.

**4** See also [[sf.cmath.cyl.bessel.j]](sf.cmath.cyl.bessel.j.md).
