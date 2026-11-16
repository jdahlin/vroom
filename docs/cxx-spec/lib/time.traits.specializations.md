# 27.4.3 Specializations of `common_type` [time.traits.specializations]

[Contents](../README.md) · up: [27.4 Time-related traits \[time.traits\]](time.traits.md) · prev: [27.4.2 `duration_values` \[time.traits.duration.values\]](time.traits.duration.values.md) · next: [27.4.4 Class template `is_clock` \[time.traits.is.clock\]](time.traits.is.clock.md)

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
struct common_type<chrono::duration<Rep1, Period1>, chrono::duration<Rep2, Period2>> {
  using type = chrono::duration<common_type_t<Rep1, Rep2>, see below>;
};
```

**1** The `period` of the `duration` indicated by this specialization of `common_type` is the greatest common divisor of `Period1` and `Period2`. [*Note 1*: This can be computed by forming a ratio of the greatest common divisor of `Period1::num` and `Period2::num` and the least common multiple of `Period1::den` and `Period2::den`. — *end note*]

**2** [*Note 2*: The `typedef` name `type` is a synonym for the `duration` with the largest tick `period` possible where both `duration` arguments will convert to it without requiring a division operation. The representation of this type is intended to be able to hold any value resulting from this conversion with no truncation error, although floating-point durations may have round-off errors. — *end note*]

```cpp
template<class Clock, class Duration1, class Duration2>
  struct common_type<chrono::time_point<Clock, Duration1>, chrono::time_point<Clock, Duration2>> {
    using type = chrono::time_point<Clock, common_type_t<Duration1, Duration2>>;
};
```

**3** The common type of two `time_point` types is a `time_point` with the same clock as the two types and the common type of their two `duration`s.
