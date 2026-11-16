# 27.7.5.2 Member functions [time.clock.file.members]

[Contents](../README.md) · up: [27.7.5 Type `file_clock` \[time.clock.file\]](time.clock.file.md) · prev: [27.7.5.1 Overview \[time.clock.file.overview\]](time.clock.file.overview.md) · next: [27.7.5.3 Non-member functions \[time.clock.file.nonmembers\]](time.clock.file.nonmembers.md)

**1** The type denoted by `file_clock` provides precisely one of the following two sets of static member functions:

```cpp
template<class Duration>
  static sys_time<see below>
    to_sys(const file_time<Duration>&);
template<class Duration>
  static file_time<see below>
    from_sys(const sys_time<Duration>&);
```

or:

```cpp
template<class Duration>
  static utc_time<see below>
    to_utc(const file_time<Duration>&);
template<class Duration>
  static file_time<see below>
    from_utc(const utc_time<Duration>&);
```

These member functions shall provide `time_point` conversions consistent with those specified by `utc_clock`, `tai_clock`, and `gps_clock`. The `Duration` of the resultant `time_point` is computed from the `Duration` of the input `time_point`.
