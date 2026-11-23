# 27.7.4.2 Member functions [time.clock.gps.members]

[Contents](../../README.md) · up: [27.7.4 Class `gps_clock` \[time.clock.gps\]](time.clock.gps.md) · prev: [27.7.4.1 Overview \[time.clock.gps.overview\]](time.clock.gps.overview.md) · next: [27.7.4.3 Non-member functions \[time.clock.gps.nonmembers\]](time.clock.gps.nonmembers.md)

```cpp
static time_point now();
```

**1** *Returns:* `from_utc(utc_clock::now())`, or a more accurate value of `gps_time`.

```cpp
template<class Duration>
  static utc_time<common_type_t<Duration, seconds>>
    to_utc(const gps_time<Duration>& t) noexcept;
```

**2** *Returns:*

```cpp
gps_time<common_type_t<Duration, seconds>>{t.time_since_epoch()} + 315964809s
```

[*Note 1*:

```cpp
315964809s == sys_days{1980y/January/Sunday[1]} - sys_days{1970y/January/1} + 9s
```

— *end note*]

```cpp
template<class Duration>
  static gps_time<common_type_t<Duration, seconds>>
    from_utc(const utc_time<Duration>& t) noexcept;
```

**3** *Returns:*

```cpp
gps_time<common_type_t<Duration, seconds>>{t.time_since_epoch()} - 315964809s
```

[*Note 2*:

```cpp
315964809s == sys_days{1980y/January/Sunday[1]} - sys_days{1970y/January/1} + 9s
```

— *end note*]
