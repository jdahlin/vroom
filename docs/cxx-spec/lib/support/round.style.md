# 17.3.4.1 Type `float_round_style` [round.style]

[Contents](../../README.md) · up: [17.3.4 Floating-point type properties \[fp.style\]](fp.style.md) · prev: [17.3.4 Floating-point type properties \[fp.style\]](fp.style.md) · next: [17.3.4.2 Type `float_denorm_style` \[denorm.style\]](denorm.style.md)

```cpp
namespace std {
  enum float_round_style {
    round_indeterminate       = -1,
    round_toward_zero         =  0,
    round_to_nearest          =  1,
    round_toward_infinity     =  2,
    round_toward_neg_infinity =  3
  };
}
```

**1** The rounding mode for floating-point arithmetic is characterized by the values:

- `round_indeterminate` if the rounding style is indeterminable
- `round_toward_zero` if the rounding style is toward zero
- `round_to_nearest` if the rounding style is to the nearest representable value
- `round_toward_infinity` if the rounding style is toward infinity
- `round_toward_neg_infinity` if the rounding style is toward negative infinity
