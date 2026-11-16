# 26.4.10 Suffixes for complex number literals [complex.literals]

[Contents](../README.md) · up: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md) · prev: [26.4.9 Additional overloads \[cmplx.over\]](cmplx.over.md) · next: [26.5 Bit manipulation \[bit\]](bit.md)

**1** This subclause describes literal suffixes for constructing complex number literals. The suffixes `i`, `il`, and `if` create complex numbers of the types `complex<double>`, `complex<long double>`, and `complex<float>` respectively, with their imaginary part denoted by the given literal number and the real part being zero.

```cpp
constexpr complex<long double> operator""il(long double d);
constexpr complex<long double> operator""il(unsigned long long d);
```

**2** *Returns:* `complex<long double>{0.0L, static_cast<long double>(d)}`.

```cpp
constexpr complex<double> operator""i(long double d);
constexpr complex<double> operator""i(unsigned long long d);
```

**3** *Returns:* `complex<double>{0.0, static_cast<double>(d)}`.

```cpp
constexpr complex<float> operator""if(long double d);
constexpr complex<float> operator""if(unsigned long long d);
```

**4** *Returns:* `complex<float>{0.0f, static_cast<float>(d)}`.
