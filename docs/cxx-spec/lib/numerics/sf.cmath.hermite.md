# 26.8.6.15 Hermite polynomials [sf.cmath.hermite]

[Contents](../../README.md) · up: [26.8.6 Mathematical special functions \[sf.cmath\]](sf.cmath.md) · prev: [26.8.6.14 Exponential integral \[sf.cmath.expint\]](sf.cmath.expint.md) · next: [26.8.6.16 Laguerre polynomials \[sf.cmath.laguerre\]](sf.cmath.laguerre.md)

```cpp
double       hermite(unsigned n, double x);
float        hermitef(unsigned n, float x);
long double  hermitel(unsigned n, long double x);
```

**1** *Effects:* These functions compute the Hermite polynomials of their respective arguments `n` and `x`.

**2** *Returns:*

$$\mathsf{H}_n(x) =
  (-1)^n e^{x^2} \frac{ \mathsf{d} ^n}
		      { \mathsf{d}x^n} \, e^{-x^2}
\;$$

where *n* is `n` and *x* is `x`.

**3** *Remarks:* The effect of calling each of these functions is implementation-defined if `n >= 128`.
