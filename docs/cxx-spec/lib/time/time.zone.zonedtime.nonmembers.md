# 27.11.7.4 Non-member functions [time.zone.zonedtime.nonmembers]

[Contents](../../README.md) · up: [27.11.7 Class template `zoned_time` \[time.zone.zonedtime\]](time.zone.zonedtime.md) · prev: [27.11.7.3 Member functions \[time.zone.zonedtime.members\]](time.zone.zonedtime.members.md) · next: [27.11.8 Class `leap_second` \[time.zone.leap\]](time.zone.leap.md)

```cpp
template<class Duration1, class Duration2, class TimeZonePtr>
  bool operator==(const zoned_time<Duration1, TimeZonePtr>& x,
                  const zoned_time<Duration2, TimeZonePtr>& y);
```

**1** *Returns:* `x.zone_ == y.zone_ && x.tp_ == y.tp_`.

```cpp
template<class charT, class traits, class Duration, class TimeZonePtr>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os,
               const zoned_time<Duration, TimeZonePtr>& t);
```

**2** *Effects:* Streams the value returned from `t.get_local_time()` to `os` using the format `"%F %T %Z"`.

**3** *Returns:* `os`.
