# 26.8.6.2 Associated Legendre functions [sf.cmath.assoc.legendre]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.1 Associated Laguerre polynomials \[sf.cmath.assoc.laguerre\]](sf.cmath.assoc.laguerre.md) · next: [26.8.6.3 Beta function \[sf.cmath.beta\]](sf.cmath.beta.md)

```cpp
double       assoc_legendre(unsigned l, unsigned m, double x);
float        assoc_legendref(unsigned l, unsigned m, float x);
long double  assoc_legendrel(unsigned l, unsigned m, long double x);
```

**1** *Effects:* These functions compute the associated Legendre functions of their respective arguments `l`, `m`, and `x`.

**2** *Returns:*

$$\mathsf{P}_\ell^m(x) = (1 - x^2) ^ {m/2} \:
   \frac{\mathsf{d} ^ m}{\mathsf{d}x ^ m} \, \mathsf{P}_\ell(x)
   \text{ ,\quad for $|x| \le 1$,}$$

where *l* is `l`, *m* is `m`, and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `l >= 128`.
