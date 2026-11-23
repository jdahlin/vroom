# 27.7.9.6 Function template `clock_cast` [time.clock.cast.fn]

[Contents](../../README.md) · up: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · prev: [27.7.9.5 Conversions between `utc_clock` and other clocks \[time.clock.cast.utc\]](time.clock.cast.utc.md) · next: [27.8 The civil calendar \[time.cal\]](time.cal.md)

```cpp
template<class DestClock, class SourceClock, class Duration>
  auto clock_cast(const time_point<SourceClock, Duration>& t);
```

**1** *Constraints:* At least one of the following clock time conversion expressions is well-formed:

- ```cpp
  clock_time_conversion<DestClock, SourceClock>{}(t)
  ```
- ```cpp
  clock_time_conversion<DestClock, system_clock>{}(
      clock_time_conversion<system_clock, SourceClock>{}(t))
  ```
- ```cpp
  clock_time_conversion<DestClock, utc_clock>{}(
      clock_time_conversion<utc_clock, SourceClock>{}(t))
  ```
- ```cpp
  clock_time_conversion<DestClock, utc_clock>{}(
      clock_time_conversion<utc_clock, system_clock>{}(
          clock_time_conversion<system_clock, SourceClock>{}(t)))
  ```
- ```cpp
  clock_time_conversion<DestClock, system_clock>{}(
      clock_time_conversion<system_clock, utc_clock>{}(
          clock_time_conversion<utc_clock, SourceClock>{}(t)))
  ```

A clock time conversion expression is considered better than another clock time conversion expression if it involves fewer `operator()` calls on `clock_time_conversion` specializations.

**2** *Mandates:* Among the well-formed clock time conversion expressions from the above list, there is a unique best expression.

**3** *Returns:* The best well-formed clock time conversion expression in the above list.
