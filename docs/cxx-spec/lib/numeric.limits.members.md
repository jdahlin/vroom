# 17.3.5.1 `numeric_limits` members [numeric.limits.members]

[Contents](../README.md) · up: [17.3.5 Class template `numeric_limits` \[numeric.limits\]](numeric.limits.md) · prev: [17.3.5 Class template `numeric_limits` \[numeric.limits\]](numeric.limits.md) · next: [17.3.5.2 `numeric_limits` specializations \[numeric.special\]](numeric.special.md)

**1** Each member function defined in this subclause is signal-safe ([[support.signal]](support.signal.md)).

```cpp
static constexpr T min() noexcept;
```

**2** Minimum finite value.[^1]

**3** For floating-point types with subnormal numbers, returns the minimum positive normalized value.

**4** Meaningful for all specializations in which `is_bounded != false`, or `is_bounded == false && is_signed == false`.

```cpp
static constexpr T max() noexcept;
```

**5** Maximum finite value.[^2]

**6** Meaningful for all specializations in which `is_bounded != false`.

```cpp
static constexpr T lowest() noexcept;
```

**7** A finite value `x` such that there is no other finite value `y` where `y < x`.[^3]

**8** Meaningful for all specializations in which `is_bounded != false`.

```cpp
static constexpr int digits;
```

**9** Number of `radix` digits that can be represented without change.

**10** For integer types, the number of non-sign bits in the representation.

**11** For floating-point types, the number of `radix` digits in the mantissa.[^4]

```cpp
static constexpr int digits10;
```

**12** Number of base 10 digits that can be represented without change.[^5]

**13** Meaningful for all specializations in which `is_bounded != false`.

```cpp
static constexpr int max_digits10;
```

**14** Number of base 10 digits required to ensure that values which differ are always differentiated.

**15** Meaningful for all floating-point types.

```cpp
static constexpr bool is_signed;
```

**16** `true` if the type is signed.

**17** Meaningful for all specializations.

```cpp
static constexpr bool is_integer;
```

**18** `true` if the type is integer.

**19** Meaningful for all specializations.

```cpp
static constexpr bool is_exact;
```

**20** `true` if the type uses an exact representation. All integer types are exact, but not all exact types are integer. For example, rational and fixed-exponent representations are exact but not integer.

**21** Meaningful for all specializations.

```cpp
static constexpr int radix;
```

**22** For floating-point types, specifies the base or radix of the exponent representation (often 2).[^6]

**23** For integer types, specifies the base of the representation.[^7]

**24** Meaningful for all specializations.

```cpp
static constexpr T epsilon() noexcept;
```

**25** Machine epsilon: the difference between 1 and the least value greater than 1 that is representable.[^8]

**26** Meaningful for all floating-point types.

```cpp
static constexpr T round_error() noexcept;
```

**27** Measure of the maximum rounding error.[^9]

```cpp
static constexpr int  min_exponent;
```

**28** Minimum negative integer such that `radix` raised to the power of one less than that integer is a normalized floating-point number.[^10]

**29** Meaningful for all floating-point types.

```cpp
static constexpr int  min_exponent10;
```

**30** Minimum negative integer such that 10 raised to that power is in the range of normalized floating-point numbers.[^11]

**31** Meaningful for all floating-point types.

```cpp
static constexpr int  max_exponent;
```

**32** Maximum positive integer such that `radix` raised to the power one less than that integer is a representable finite floating-point number.[^12]

**33** Meaningful for all floating-point types.

```cpp
static constexpr int  max_exponent10;
```

**34** Maximum positive integer such that 10 raised to that power is in the range of representable finite floating-point numbers.[^13]

**35** Meaningful for all floating-point types.

```cpp
static constexpr bool has_infinity;
```

**36** `true` if the type has a representation for positive infinity.

**37** Meaningful for all floating-point types.

**38** Shall be `true` for all specializations in which `is_iec559 != false`.

```cpp
static constexpr bool has_quiet_NaN;
```

**39** `true` if the type has a representation for a quiet (non-signaling) “Not a Number”.[^14]

**40** Meaningful for all floating-point types.

**41** Shall be `true` for all specializations in which `is_iec559 != false`.

```cpp
static constexpr bool has_signaling_NaN;
```

**42** `true` if the type has a representation for a signaling “Not a Number”.[^15]

**43** Meaningful for all floating-point types.

**44** Shall be `true` for all specializations in which `is_iec559 != false`.

```cpp
static constexpr float_denorm_style has_denorm;
```

**45** `denorm_present` if the type allows subnormal values (variable number of exponent bits)[^16], `denorm_absent` if the type does not allow subnormal values, and `denorm_indeterminate` if it is indeterminate at compile time whether the type allows subnormal values.

**46** Meaningful for all floating-point types.

```cpp
static constexpr bool has_denorm_loss;
```

**47** `true` if loss of accuracy is detected as a denormalization loss, rather than as an inexact result.[^17]

```cpp
static constexpr T infinity() noexcept;
```

**48** Representation of positive infinity, if available.[^18]

**49** Meaningful for all specializations for which `has_infinity != false`. Required in specializations for which `is_iec559 != false`.

```cpp
static constexpr T quiet_NaN() noexcept;
```

**50** Representation of a quiet “Not a Number”, if available.[^19]

**51** Meaningful for all specializations for which `has_quiet_NaN != false`. Required in specializations for which `is_iec559 != false`.

```cpp
static constexpr T signaling_NaN() noexcept;
```

**52** Representation of a signaling “Not a Number”, if available.[^20]

**53** Meaningful for all specializations for which `has_signaling_NaN != false`. Required in specializations for which `is_iec559 != false`.

```cpp
static constexpr T denorm_min() noexcept;
```

**54** Minimum positive subnormal value.[^21]

**55** Meaningful for all floating-point types.

**56** In specializations for which `has_denorm == false`, returns the minimum positive normalized value.

```cpp
static constexpr bool is_iec559;
```

**57** `true` if and only if the type adheres to ISO/IEC/IEEE 60559.[^22]

**58** Meaningful for all floating-point types.

```cpp
static constexpr bool is_bounded;
```

**59** `true` if the set of values representable by the type is finite.[^23] [*Note 1*: All fundamental types ([[basic.fundamental]](../basic.fundamental.md)) are bounded. This member would be `false` for arbitrary precision types. — *end note*]

**60** Meaningful for all specializations.

```cpp
static constexpr bool is_modulo;
```

**61** `true` if the type is modulo.[^24] A type is modulo if, for any operation involving `+`, `-`, or `*` on values of that type whose result would fall outside the range `[min(), max()]`, the value returned differs from the true value by an integer multiple of `max() - min() + 1`.

**62** [*Example 1*: `is_modulo` is `false` for signed integer types ([[basic.fundamental]](../basic.fundamental.md)) unless an implementation, as an extension to this document, defines signed integer overflow to wrap. — *end example*]

**63** Meaningful for all specializations.

```cpp
static constexpr bool traps;
```

**64** `true` if, at the start of the program, there exists a value of the type that would cause an arithmetic operation using that value to trap.[^25]

**65** Meaningful for all specializations.

```cpp
static constexpr bool tinyness_before;
```

**66** `true` if tinyness is detected before rounding.[^26]

**67** Meaningful for all floating-point types.

```cpp
static constexpr float_round_style round_style;
```

**68** The rounding style for the type.[^27]

**69** Meaningful for all floating-point types. Specializations for integer types shall return `round_toward_zero`.

[^1]: Equivalent to `CHAR_MIN`, `SHRT_MIN`, `FLT_MIN`, `DBL_MIN`, etc.
[^2]: Equivalent to `CHAR_MAX`, `SHRT_MAX`, `FLT_MAX`, `DBL_MAX`, etc.
[^3]: `lowest()` is necessary because not all floating-point representations have a smallest (most negative) value that is the negative of the largest (most positive) finite value.
[^4]: Equivalent to `FLT_MANT_DIG`, `DBL_MANT_DIG`, `LDBL_MANT_DIG`.
[^5]: Equivalent to `FLT_DIG`, `DBL_DIG`, `LDBL_DIG`.
[^6]: Equivalent to `FLT_RADIX`.
[^7]: Distinguishes types with bases other than 2 (e.g. BCD).
[^8]: Equivalent to `FLT_EPSILON`, `DBL_EPSILON`, `LDBL_EPSILON`.
[^9]: Rounding error is described in LIA-1 Section 5.2.4 and Annex C Rationale Section C.5.2.4 — Rounding and rounding constants.
[^10]: Equivalent to `FLT_MIN_EXP`, `DBL_MIN_EXP`, `LDBL_MIN_EXP`.
[^11]: Equivalent to `FLT_MIN_10_EXP`, `DBL_MIN_10_EXP`, `LDBL_MIN_10_EXP`.
[^12]: Equivalent to `FLT_MAX_EXP`, `DBL_MAX_EXP`, `LDBL_MAX_EXP`.
[^13]: Equivalent to `FLT_MAX_10_EXP`, `DBL_MAX_10_EXP`, `LDBL_MAX_10_EXP`.
[^14]: Required by LIA-1.
[^15]: Required by LIA-1.
[^16]: Required by LIA-1.
[^17]: See ISO/IEC/IEEE 60559.
[^18]: Required by LIA-1.
[^19]: Required by LIA-1.
[^20]: Required by LIA-1.
[^21]: Required by LIA-1.
[^22]: ISO/IEC/IEEE 60559:2011 is the same as IEEE 754-2008.
[^23]: Required by LIA-1.
[^24]: Required by LIA-1.
[^25]: Required by LIA-1.
[^26]: Refer to ISO/IEC/IEEE 60559. Required by LIA-1.
[^27]: Equivalent to `FLT_ROUNDS`. Required by LIA-1.
