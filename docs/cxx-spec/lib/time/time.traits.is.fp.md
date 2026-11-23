# 27.4.1 `treat_as_floating_point` [time.traits.is.fp]

[Contents](../../README.md) · up: [27.4 Time-related traits \[time.traits\]](time.traits.md) · prev: [27.4 Time-related traits \[time.traits\]](time.traits.md) · next: [27.4.2 `duration_values` \[time.traits.duration.values\]](time.traits.duration.values.md)

```cpp
template<class Rep> struct treat_as_floating_point : is_floating_point<Rep> { };
```

**1** The `duration` template uses the `treat_as_floating_point` trait to help determine if a `duration` object can be converted to another `duration` with a different tick `period`. If `treat_as_floating_point_v<Rep>` is `true`, then implicit conversions are allowed among `duration`s. Otherwise, the implicit convertibility depends on the tick `period`s of the `duration`s. [*Note 1*: The intention of this trait is to indicate whether a given class behaves like a floating-point type, and thus allows division of one value by another with acceptable loss of precision. If `treat_as_floating_point_v<Rep>` is `false`, `Rep` will be treated as if it behaved like an integral type for the purpose of these conversions. — *end note*]
