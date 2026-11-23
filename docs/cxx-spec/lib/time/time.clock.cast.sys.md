# 27.7.9.4 Conversions between `system_clock` and other clocks [time.clock.cast.sys]

[Contents](../../README.md) · up: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · prev: [27.7.9.3 Conversions between `system_clock` and `utc_clock` \[time.clock.cast.sys.utc\]](time.clock.cast.sys.utc.md) · next: [27.7.9.5 Conversions between `utc_clock` and other clocks \[time.clock.cast.utc\]](time.clock.cast.utc.md)

```cpp
template<class SourceClock>
struct clock_time_conversion<system_clock, SourceClock> {
  template<class Duration>
    auto operator()(const time_point<SourceClock, Duration>& t) const
      -> decltype(SourceClock::to_sys(t));
};
```

```cpp
template<class Duration>
  auto operator()(const time_point<SourceClock, Duration>& t) const
    -> decltype(SourceClock::to_sys(t));
```

**1** *Constraints:* `SourceClock::to_sys(t)` is well-formed.

**2** *Mandates:* `SourceClock::to_sys(t)` returns a `sys_time<Duration>`, where `Duration` is a valid `chrono::duration` specialization.

**3** *Returns:* `SourceClock::to_sys(t)`.

```cpp
template<class DestClock>
struct clock_time_conversion<DestClock, system_clock> {
  template<class Duration>
    auto operator()(const sys_time<Duration>& t) const
      -> decltype(DestClock::from_sys(t));
};
```

```cpp
template<class Duration>
  auto operator()(const sys_time<Duration>& t) const
    -> decltype(DestClock::from_sys(t));
```

**4** *Constraints:* `DestClock::from_sys(t)` is well-formed.

**5** *Mandates:* `DestClock::from_sys(t)` returns a `time_point<DestClock, Duration>`, where `Duration` is a valid `chrono::duration` specialization.

**6** *Returns:* `DestClock::from_sys(t)`.
