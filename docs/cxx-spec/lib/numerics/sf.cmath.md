# 26.8.6 Mathematical special functions [sf.cmath]

[Contents](../../README.md) · up: [26.8 Mathematical functions for floating-point types \[c.math\]](c.math.md) · prev: [26.8.5 Classification / comparison functions \[c.math.fpclass\]](c.math.fpclass.md) · next: [26.8.6.1 Associated Laguerre polynomials \[sf.cmath.assoc.laguerre\]](sf.cmath.assoc.laguerre.md)

**1** If any argument value to any of the functions specified in this subclause is a NaN (Not a Number), the function shall return a NaN but it shall not report a domain error. Otherwise, the function shall report a domain error for just those argument values for which:

- the function description’s *Returns:* clause explicitly specifies a domain and those argument values fall outside the specified domain, or
- the corresponding mathematical function value has a nonzero imaginary component, or
- the corresponding mathematical function is not mathematically defined.[^1]

**2** Unless otherwise specified, each function is defined for all finite values, for negative infinity, and for positive infinity.

## Subclauses

- [26.8.6.1 Associated Laguerre polynomials \[sf.cmath.assoc.laguerre\]](sf.cmath.assoc.laguerre.md)
- [26.8.6.2 Associated Legendre functions \[sf.cmath.assoc.legendre\]](sf.cmath.assoc.legendre.md)
- [26.8.6.3 Beta function \[sf.cmath.beta\]](sf.cmath.beta.md)
- [26.8.6.4 Complete elliptic integral of the first kind \[sf.cmath.comp.ellint.1\]](sf.cmath.comp.ellint.1.md)
- [26.8.6.5 Complete elliptic integral of the second kind \[sf.cmath.comp.ellint.2\]](sf.cmath.comp.ellint.2.md)
- [26.8.6.6 Complete elliptic integral of the third kind \[sf.cmath.comp.ellint.3\]](sf.cmath.comp.ellint.3.md)
- [26.8.6.7 Regular modified cylindrical Bessel functions \[sf.cmath.cyl.bessel.i\]](sf.cmath.cyl.bessel.i.md)
- [26.8.6.8 Cylindrical Bessel functions of the first kind \[sf.cmath.cyl.bessel.j\]](sf.cmath.cyl.bessel.j.md)
- [26.8.6.9 Irregular modified cylindrical Bessel functions \[sf.cmath.cyl.bessel.k\]](sf.cmath.cyl.bessel.k.md)
- [26.8.6.10 Cylindrical Neumann functions \[sf.cmath.cyl.neumann\]](sf.cmath.cyl.neumann.md)
- [26.8.6.11 Incomplete elliptic integral of the first kind \[sf.cmath.ellint.1\]](sf.cmath.ellint.1.md)
- [26.8.6.12 Incomplete elliptic integral of the second kind \[sf.cmath.ellint.2\]](sf.cmath.ellint.2.md)
- [26.8.6.13 Incomplete elliptic integral of the third kind \[sf.cmath.ellint.3\]](sf.cmath.ellint.3.md)
- [26.8.6.14 Exponential integral \[sf.cmath.expint\]](sf.cmath.expint.md)
- [26.8.6.15 Hermite polynomials \[sf.cmath.hermite\]](sf.cmath.hermite.md)
- [26.8.6.16 Laguerre polynomials \[sf.cmath.laguerre\]](sf.cmath.laguerre.md)
- [26.8.6.17 Legendre polynomials \[sf.cmath.legendre\]](sf.cmath.legendre.md)
- [26.8.6.18 Riemann zeta function \[sf.cmath.riemann.zeta\]](sf.cmath.riemann.zeta.md)
- [26.8.6.19 Spherical Bessel functions of the first kind \[sf.cmath.sph.bessel\]](sf.cmath.sph.bessel.md)
- [26.8.6.20 Spherical associated Legendre functions \[sf.cmath.sph.legendre\]](sf.cmath.sph.legendre.md)
- [26.8.6.21 Spherical Neumann functions \[sf.cmath.sph.neumann\]](sf.cmath.sph.neumann.md)

[^1]: A mathematical function is mathematically defined for a given set of argument values (a) if it is explicitly defined for that set of argument values, or (b) if its limiting value exists and does not depend on the direction of approach.
