# 20.18.8 Execution policy objects [execpol.objects]

[Contents](../../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.7 Unsequenced execution policy \[execpol.unseq\]](execpol.unseq.md) · next: [20.19 Primitive numeric conversions \[charconv\]](charconv.md)

```cpp
inline constexpr execution::sequenced_policy            execution::seq{ unspecified };
inline constexpr execution::parallel_policy             execution::par{ unspecified };
inline constexpr execution::parallel_unsequenced_policy execution::par_unseq{ unspecified };
inline constexpr execution::unsequenced_policy          execution::unseq{ unspecified };
```

**1** The header `<execution>` declares global objects associated with each type of execution policy.
