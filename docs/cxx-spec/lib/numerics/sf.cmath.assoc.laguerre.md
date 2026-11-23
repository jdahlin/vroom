# 26.8.6.1 Associated Laguerre polynomials [sf.cmath.assoc.laguerre]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · next: [26.8.6.2 Associated Legendre functions \[sf.cmath.assoc.legendre\]](sf.cmath.assoc.legendre.md)

```cpp
double       assoc_laguerre(unsigned n, unsigned m, double x);
float        assoc_laguerref(unsigned n, unsigned m, float x);
long double  assoc_laguerrel(unsigned n, unsigned m, long double x);
```

**1** *Effects:* These functions compute the associated Laguerre polynomials of their respective arguments `n`, `m`, and `x`.

**2** *Returns:*

$$\mathsf{L}_n^m(x) =
   (-1)^m \frac{\mathsf{d} ^ m}{\mathsf{d}x ^ m} \, \mathsf{L}_{n+m}(x)
   \text{ ,\quad for $x \ge 0$,}$$

where *n* is `n`, *m* is `m`, and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `n >= 128` or if `m >= 128`.
