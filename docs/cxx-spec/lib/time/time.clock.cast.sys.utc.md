# 27.7.9.3 Conversions between `system_clock` and `utc_clock` [time.clock.cast.sys.utc]

[Contents](../../README.md) · up: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md) · prev: [27.7.9.2 Identity conversions \[time.clock.cast.id\]](time.clock.cast.id.md) · next: [27.7.9.4 Conversions between `system_clock` and other clocks \[time.clock.cast.sys\]](time.clock.cast.sys.md)

```cpp
template<>
struct clock_time_conversion<utc_clock, system_clock> {
  template<class Duration>
    utc_time<common_type_t<Duration, seconds>>
      operator()(const sys_time<Duration>& t) const;
};
```

```cpp
template<class Duration>
  utc_time<common_type_t<Duration, seconds>>
    operator()(const sys_time<Duration>& t) const;
```

**1** *Returns:* `utc_clock::from_sys(t)`.

```cpp
template<>
struct clock_time_conversion<system_clock, utc_clock> {
  template<class Duration>
    sys_time<common_type_t<Duration, seconds>>
      operator()(const utc_time<Duration>& t) const;
};
```

```cpp
template<class Duration>
  sys_time<common_type_t<Duration, seconds>>
    operator()(const utc_time<Duration>& t) const;
```

**2** *Returns:* `utc_clock::to_sys(t)`.
