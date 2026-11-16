# 27.11.7.3 Member functions [time.zone.zonedtime.members]

[Contents](../README.md) · up: [27.11.7 Class template `zoned_time` \[time.zone.zonedtime\]](time.zone.zonedtime.md) · prev: [27.11.7.2 Constructors \[time.zone.zonedtime.ctor\]](time.zone.zonedtime.ctor.md) · next: [27.11.7.4 Non-member functions \[time.zone.zonedtime.nonmembers\]](time.zone.zonedtime.nonmembers.md)

```cpp
zoned_time& operator=(const sys_time<Duration>& st);
```

**1** *Effects:* After assignment, `get_sys_time() == st`. This assignment has no effect on the return value of `get_time_zone()`.

**2** *Returns:* `*this`.

```cpp
zoned_time& operator=(const local_time<Duration>& lt);
```

**3** *Effects:* After assignment, `get_local_time() == lt`. This assignment has no effect on the return value of `get_time_zone()`.

**4** *Returns:* `*this`.

```cpp
operator sys_time<duration>() const;
```

**5** *Returns:* `get_sys_time()`.

```cpp
explicit operator local_time<duration>() const;
```

**6** *Returns:* `get_local_time()`.

```cpp
TimeZonePtr get_time_zone() const;
```

**7** *Returns:* `zone_`.

```cpp
local_time<duration> get_local_time() const;
```

**8** *Returns:* `zone_->to_local(tp_)`.

```cpp
sys_time<duration> get_sys_time() const;
```

**9** *Returns:* `tp_`.

```cpp
sys_info get_info() const;
```

**10** *Returns:* `zone_->get_info(tp_)`.
