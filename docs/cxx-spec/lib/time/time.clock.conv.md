# 27.7.9.1 Class template `clock_time_conversion` [time.clock.conv]

[Contents](../../README.md) · up: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · prev: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · next: [27.7.9.2 Identity conversions \[time.clock.cast.id\]](time.clock.cast.id.md)

```cpp
namespace std::chrono {
  template<class DestClock, class SourceClock>
  struct clock_time_conversion {};
}
```

**1** `clock_time_conversion` serves as a trait which can be used to specify how to convert a source `time_point` of type `time_point<SourceClock, Duration>` to a destination `time_point` of type `time_point<DestClock, Duration>` via a specialization: `clock_time_conversion<DestClock, SourceClock>`. A specialization of `clock_time_conversion<DestClock, SourceClock>` shall provide a const-qualified `operator()` that takes a parameter of type `time_point<SourceClock, Duration>` and returns a `time_point<DestClock, OtherDuration>` representing an equivalent point in time. `OtherDuration` is a `chrono::duration` whose specialization is computed from the input `Duration` in a manner which can vary for each `clock_time_conversion` specialization. A program may specialize `clock_time_conversion` if at least one of the template parameters is a user-defined clock type.

**2** Several specializations are provided by the implementation, as described in [[time.clock.cast.id]](time.clock.cast.id.md), [[time.clock.cast.sys.utc]](time.clock.cast.sys.utc.md), [[time.clock.cast.sys]](time.clock.cast.sys.md), and [[time.clock.cast.utc]](time.clock.cast.utc.md).
