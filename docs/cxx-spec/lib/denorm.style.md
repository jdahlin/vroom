# 17.3.4.2 Type `float_denorm_style` [denorm.style]

[Contents](../README.md) · up: [17.3.4 Floating-point type properties \[fp.style\]](fp.style.md) · prev: [17.3.4.1 Type `float_round_style` \[round.style\]](round.style.md) · next: [17.3.5 Class template `numeric_limits` \[numeric.limits\]](numeric.limits.md)

```cpp
namespace std {
  enum float_denorm_style {
    denorm_indeterminate = -1,
    denorm_absent = 0,
    denorm_present = 1
  };
}
```

**1** The presence or absence of subnormal numbers (variable number of exponent bits) is characterized by the values:

- `denorm_indeterminate` if it cannot be determined whether or not the type allows subnormal values
- `denorm_absent` if the type does not allow subnormal values
- `denorm_present` if the type does allow subnormal values
