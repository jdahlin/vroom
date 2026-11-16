# 26.4.9 Additional overloads [cmplx.over]

[Contents](../../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.8 Transcendentals \[complex.transcendentals\]](complex.transcendentals.md) · next: [26.4.10 Suffixes for complex number literals \[complex.literals\]](complex.literals.md)

**1** The following function templates shall have additional overloads:

```cpp
arg                   norm
conj                  proj
imag                  real
```

where `norm`, `conj`, `imag`, and `real` are `constexpr` overloads.

**2** The additional overloads shall be sufficient to ensure:

- If the argument has type `long double`, then it is effectively cast to `complex<long double>`.
- Otherwise, if the argument has type `double` or an integer type, then it is effectively cast to `complex<double>`.
- Otherwise, if the argument has type `float`, then it is effectively cast to `complex<float>`.

**3** Function template `pow` shall have additional overloads sufficient to ensure, for a call with at least one argument of type `complex<T>`:

- If either argument has type `complex<long double>` or type `long double`, then both arguments are effectively cast to `complex<long double>`.
- Otherwise, if either argument has type `complex<double>`, `double`, or an integer type, then both arguments are effectively cast to `complex<double>`.
- Otherwise, if either argument has type `complex<float>` or `float`, then both arguments are effectively cast to `complex<float>`.
