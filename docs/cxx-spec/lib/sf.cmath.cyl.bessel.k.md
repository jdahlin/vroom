# 26.8.6.9 Irregular modified cylindrical Bessel functions [sf.cmath.cyl.bessel.k]

[Contents](../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.8 Cylindrical Bessel functions of the first kind \[sf.cmath.cyl.bessel.j\]](sf.cmath.cyl.bessel.j.md) · next: [26.8.6.10 Cylindrical Neumann functions \[sf.cmath.cyl.neumann\]](sf.cmath.cyl.neumann.md)

```cpp
double       cyl_bessel_k(double nu, double x);
float        cyl_bessel_kf(float nu, float x);
long double  cyl_bessel_kl(long double nu, long double x);
```

**1** *Effects:* These functions compute the irregular modified cylindrical Bessel functions of their respective arguments `nu` and `x`.

**2** *Returns:*

$$\mathsf{K}_\nu(x) =
  (\pi/2)\mathrm{i}^{\nu+1} (            \mathsf{J}_\nu(\mathrm{i}x)
			    + \mathrm{i} \mathsf{N}_\nu(\mathrm{i}x)
			    )
  =
  \left\{
  \begin{array}{cl}
  \displaystyle
  \frac{\pi}{2}
  \frac{\mathsf{I}_{-\nu}(x) - \mathsf{I}_{\nu}(x)}
       {\sin \nu\pi },
  & \mbox{for $x \ge 0$ and non-integral $\nu$}
  \\
  \\
  \displaystyle
  \frac{\pi}{2}
  \lim_{\mu \rightarrow \nu} \frac{\mathsf{I}_{-\mu}(x) - \mathsf{I}_{\mu}(x)}
                                  {\sin \mu\pi },
  & \mbox{for $x \ge 0$ and integral $\nu$}
  \end{array}
  \right.$$

where ν is `nu` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `nu >= 128`.

**4** See also [[sf.cmath.cyl.bessel.i]](sf.cmath.cyl.bessel.i.md), [[sf.cmath.cyl.bessel.j]](sf.cmath.cyl.bessel.j.md), [[sf.cmath.cyl.neumann]](sf.cmath.cyl.neumann.md).
