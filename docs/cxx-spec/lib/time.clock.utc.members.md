# 27.7.2.2 Member functions [time.clock.utc.members]

[Contents](../README.md) · up: [27.7.2 Class `utc_clock` \[time.clock.utc\]](time.clock.utc.md) · prev: [27.7.2.1 Overview \[time.clock.utc.overview\]](time.clock.utc.overview.md) · next: [27.7.2.3 Non-member functions \[time.clock.utc.nonmembers\]](time.clock.utc.nonmembers.md)

```cpp
static time_point now();
```

**1** *Returns:* `from_sys(system_clock::now())`, or a more accurate value of `utc_time`.

```cpp
template<class Duration>
  static sys_time<common_type_t<Duration, seconds>>
    to_sys(const utc_time<Duration>& u);
```

**2** *Returns:* A `sys_time` `t`, such that `from_sys(t) == u` if such a mapping exists. Otherwise `u` represents a `time_point` during a positive leap second insertion, the conversion counts that leap second as not inserted, and the last representable value of `sys_time` prior to the insertion of the leap second is returned.

```cpp
template<class Duration>
  static utc_time<common_type_t<Duration, seconds>>
    from_sys(const sys_time<Duration>& t);
```

**3** *Returns:* A `utc_time` `u`, such that `u.time_since_epoch() - t.time_since_epoch()` is equal to the sum of leap seconds that were inserted between `t` and 1970-01-01. If `t` is exactly the date of leap second insertion, then the conversion counts that leap second as inserted.

[*Example 1*:

```cpp
auto t = sys_days{July/1/2015} - 2ns;
auto u = utc_clock::from_sys(t);
assert(u.time_since_epoch() - t.time_since_epoch() == 25s);
t += 1ns;
u = utc_clock::from_sys(t);
assert(u.time_since_epoch() - t.time_since_epoch() == 25s);
t += 1ns;
u = utc_clock::from_sys(t);
assert(u.time_since_epoch() - t.time_since_epoch() == 26s);
t += 1ns;
u = utc_clock::from_sys(t);
assert(u.time_since_epoch() - t.time_since_epoch() == 26s);
```

— *end example*]
