# 17.3.5 Class template `numeric_limits` [numeric.limits]

[Contents](../../README.md) · up: [17.3 Implementation properties \[support.limits\]](support.limits.md) · prev: [17.3.4.2 Type `float_denorm_style` \[denorm.style\]](denorm.style.md) · next: [17.3.5.1 `numeric_limits` members \[numeric.limits.members\]](numeric.limits.members.md)

**1** The `numeric_limits` class template provides a C++ program with information about various properties of the implementation’s representation of the arithmetic types.

```cpp
namespace std {
  template<class T> class numeric_limits {
  public:
    static constexpr bool is_specialized = false;
    static constexpr T min() noexcept { return T(); }
    static constexpr T max() noexcept { return T(); }
    static constexpr T lowest() noexcept { return T(); }

    static constexpr int  digits = 0;
    static constexpr int  digits10 = 0;
    static constexpr int  max_digits10 = 0;
    static constexpr bool is_signed = false;
    static constexpr bool is_integer = false;
    static constexpr bool is_exact = false;
    static constexpr int  radix = 0;
    static constexpr T epsilon() noexcept { return T(); }
    static constexpr T round_error() noexcept { return T(); }

    static constexpr int  min_exponent = 0;
    static constexpr int  min_exponent10 = 0;
    static constexpr int  max_exponent = 0;
    static constexpr int  max_exponent10 = 0;

    static constexpr bool has_infinity = false;
    static constexpr bool has_quiet_NaN = false;
    static constexpr bool has_signaling_NaN = false;
    static constexpr float_denorm_style has_denorm = denorm_absent;
    static constexpr bool has_denorm_loss = false;
    static constexpr T infinity() noexcept { return T(); }
    static constexpr T quiet_NaN() noexcept { return T(); }
    static constexpr T signaling_NaN() noexcept { return T(); }
    static constexpr T denorm_min() noexcept { return T(); }

    static constexpr bool is_iec559 = false;
    static constexpr bool is_bounded = false;
    static constexpr bool is_modulo = false;

    static constexpr bool traps = false;
    static constexpr bool tinyness_before = false;
    static constexpr float_round_style round_style = round_toward_zero;
  };
}
```

**2** For all members declared `static` `constexpr` in the `numeric_limits` template, specializations shall define these values in such a way that they are usable as constant expressions.

**3** The default `numeric_limits<T>` template shall have all members, but with 0 or `false` values.

**4** Specializations shall be provided for each arithmetic type, both floating-point and integer, including `bool`. The member `is_specialized` shall be `true` for all such specializations of `numeric_limits`.

**5** The value of each member of a specialization of `numeric_limits` on a cv-qualified type `cv T` shall be equal to the value of the corresponding member of the specialization on the unqualified type `T`.

**6** Non-arithmetic standard types, such as `complex<T>` ([[complex]](../numerics/complex.md)), shall not have specializations.

## Subclauses

- [17.3.5.1 `numeric_limits` members \[numeric.limits.members\]](numeric.limits.members.md)
- [17.3.5.2 `numeric_limits` specializations \[numeric.special\]](numeric.special.md)
