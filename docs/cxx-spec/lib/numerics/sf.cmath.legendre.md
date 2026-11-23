# 26.8.6.17 Legendre polynomials [sf.cmath.legendre]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.16 Laguerre polynomials \[sf.cmath.laguerre\]](sf.cmath.laguerre.md) · next: [26.8.6.18 Riemann zeta function \[sf.cmath.riemann.zeta\]](sf.cmath.riemann.zeta.md)

```cpp
double       legendre(unsigned l, double x);
float        legendref(unsigned l, float x);
long double  legendrel(unsigned l, long double x);
```

**1** *Effects:* These functions compute the Legendre polynomials of their respective arguments `l` and `x`.

**2** *Returns:*

$$\mathsf{P}_\ell(x) =
     \frac{1}{2^\ell \, \ell!}
     \frac{\mathsf{d}^\ell}{\mathsf{d}x^\ell} \, (x^2 - 1) ^ \ell
     \text{ ,\quad for $|x| \le 1$,}$$

where *l* is `l` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `l >= 128`.
