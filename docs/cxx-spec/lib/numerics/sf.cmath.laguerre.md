# 26.8.6.16 Laguerre polynomials [sf.cmath.laguerre]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.15 Hermite polynomials \[sf.cmath.hermite\]](sf.cmath.hermite.md) · next: [26.8.6.17 Legendre polynomials \[sf.cmath.legendre\]](sf.cmath.legendre.md)

```cpp
double       laguerre(unsigned n, double x);
float        laguerref(unsigned n, float x);
long double  laguerrel(unsigned n, long double x);
```

**1** *Effects:* These functions compute the Laguerre polynomials of their respective arguments `n` and `x`.

**2** *Returns:*

$$\mathsf{L}_n(x) =
     \frac{e^x}{n!} \frac{\mathsf{d}^n}{\mathsf{d}x^n} \, (x^n e^{-x})
     \text{ ,\quad for $x \ge 0$,}$$

where *n* is `n` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `n >= 128`.
