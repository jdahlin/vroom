# 27.7.9.2 Identity conversions [time.clock.cast.id]

[Contents](../../README.md) · up: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · prev: [27.7.9.1 Class template `clock_time_conversion` \[time.clock.conv\]](time.clock.conv.md) · next: [27.7.9.3 Conversions between `system_clock` and `utc_clock` \[time.clock.cast.sys.utc\]](time.clock.cast.sys.utc.md)

```cpp
template<class Clock>
struct clock_time_conversion<Clock, Clock> {
  template<class Duration>
    time_point<Clock, Duration>
      operator()(const time_point<Clock, Duration>& t) const;
};
```

```cpp
template<class Duration>
  time_point<Clock, Duration>
    operator()(const time_point<Clock, Duration>& t) const;
```

**1** *Returns:* `t`.

```cpp
template<>
struct clock_time_conversion<system_clock, system_clock> {
  template<class Duration>
    sys_time<Duration>
      operator()(const sys_time<Duration>& t) const;
};
```

```cpp
template<class Duration>
  sys_time<Duration>
    operator()(const sys_time<Duration>& t) const;
```

**2** *Returns:* `t`.

```cpp
template<>
struct clock_time_conversion<utc_clock, utc_clock> {
  template<class Duration>
    utc_time<Duration>
      operator()(const utc_time<Duration>& t) const;
};
```

```cpp
template<class Duration>
  utc_time<Duration>
    operator()(const utc_time<Duration>& t) const;
```

**3** *Returns:* `t`.
