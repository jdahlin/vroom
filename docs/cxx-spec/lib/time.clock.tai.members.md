# 27.7.3.2 Member functions [time.clock.tai.members]

[Contents](../README.md) · up: [27.7.3 Class `tai_clock` \[time.clock.tai\]](time.clock.tai.md) · prev: [27.7.3.1 Overview \[time.clock.tai.overview\]](time.clock.tai.overview.md) · next: [27.7.3.3 Non-member functions \[time.clock.tai.nonmembers\]](time.clock.tai.nonmembers.md)

```cpp
static time_point now();
```

**1** *Returns:* `from_utc(utc_clock::now())`, or a more accurate value of `tai_time`.

```cpp
template<class Duration>
  static utc_time<common_type_t<Duration, seconds>>
    to_utc(const tai_time<Duration>& t) noexcept;
```

**2** *Returns:*

```cpp
utc_time<common_type_t<Duration, seconds>>{t.time_since_epoch()} - 378691210s
```

[*Note 1*:

```cpp
378691210s == sys_days{1970y/January/1} - sys_days{1958y/January/1} + 10s
```

— *end note*]

```cpp
template<class Duration>
  static tai_time<common_type_t<Duration, seconds>>
    from_utc(const utc_time<Duration>& t) noexcept;
```

**3** *Returns:*

```cpp
tai_time<common_type_t<Duration, seconds>>{t.time_since_epoch()} + 378691210s
```

[*Note 2*:

```cpp
378691210s == sys_days{1970y/January/1} - sys_days{1958y/January/1} + 10s
```

— *end note*]
