# 27.7.9.5 Conversions between `utc_clock` and other clocks [time.clock.cast.utc]

[Contents](../README.md) · up: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · prev: [27.7.9.4 Conversions between `system_clock` and other clocks \[time.clock.cast.sys\]](time.clock.cast.sys.md) · next: [27.7.9.6 Function template `clock_cast` \[time.clock.cast.fn\]](time.clock.cast.fn.md)

```cpp
template<class SourceClock>
struct clock_time_conversion<utc_clock, SourceClock> {
  template<class Duration>
    auto operator()(const time_point<SourceClock, Duration>& t) const
      -> decltype(SourceClock::to_utc(t));
};
```

```cpp
template<class Duration>
  auto operator()(const time_point<SourceClock, Duration>& t) const
    -> decltype(SourceClock::to_utc(t));
```

**1** *Constraints:* `SourceClock::to_utc(t)` is well-formed.

**2** *Mandates:* `SourceClock::to_utc(t)` returns a `utc_time<Duration>`, where `Duration` is a valid `chrono::duration` specialization.

**3** *Returns:* `SourceClock::to_utc(t)`.

```cpp
template<class DestClock>
struct clock_time_conversion<DestClock, utc_clock> {
  template<class Duration>
    auto operator()(const utc_time<Duration>& t) const
      -> decltype(DestClock::from_utc(t));
};
```

```cpp
template<class Duration>
  auto operator()(const utc_time<Duration>& t) const
    -> decltype(DestClock::from_utc(t));
```

**4** *Constraints:* `DestClock::from_utc(t)` is well-formed.

**5** *Mandates:* `DestClock::from_utc(t)` returns a `time_point<DestClock, Duration>`, where `Duration` is a valid `chrono::duration` specialization.

**6** *Returns:* `DestClock::from_utc(t)`.
