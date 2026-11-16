# 20.16.3 Class template `ratio` [ratio.ratio]

[Contents](../../README.md) · up: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md) · prev: [20.16.2 Header `<ratio>` synopsis \[ratio.syn\]](ratio.syn.md) · next: [20.16.4 Arithmetic on `ratio`s \[ratio.arithmetic\]](ratio.arithmetic.md)

```cpp
namespace std {
  template<intmax_t N, intmax_t D = 1> class ratio {
  public:
    static constexpr intmax_t num;
    static constexpr intmax_t den;
    using type = ratio<num, den>;
  };
}
```

**1** If the template argument `D` is zero or the absolute values of either of the template arguments `N` and `D` is not representable by type `intmax_t`, the program is ill-formed. [*Note 1*: These rules ensure that infinite ratios are avoided and that for any negative input, there exists a representable value of its absolute value which is positive. This excludes the most negative value. — *end note*]

**2** The static data members `num` and `den` shall have the following values, where `gcd` represents the greatest common divisor of the absolute values of `N` and `D`:

- `num` shall have the value `sign(N) * sign(D) * abs(N) / gcd`.
- `den` shall have the value `abs(D) / gcd`.
