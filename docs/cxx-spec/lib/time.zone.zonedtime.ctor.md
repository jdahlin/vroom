# 27.11.7.2 Constructors [time.zone.zonedtime.ctor]

[Contents](../README.md) · up: [27.11.7 Class template `zoned_time` \[time.zone.zonedtime\]](time.zone.zonedtime.md) · prev: [27.11.7.1 Overview \[time.zone.zonedtime.overview\]](time.zone.zonedtime.overview.md) · next: [27.11.7.3 Member functions \[time.zone.zonedtime.members\]](time.zone.zonedtime.members.md)

```cpp
zoned_time();
```

**1** *Constraints:* `traits::default_zone()` is a well-formed expression.

**2** *Effects:* Initializes `zone_` with `traits::default_zone()` and default constructs `tp_`.

```cpp
zoned_time(const sys_time<Duration>& st);
```

**3** *Constraints:* `traits::default_zone()` is a well-formed expression.

**4** *Effects:* Initializes `zone_` with `traits::default_zone()` and `tp_` with `st`.

```cpp
explicit zoned_time(TimeZonePtr z);
```

**5** *Preconditions:* `z` refers to a time zone.

**6** *Effects:* Initializes `zone_` with `std::move(z)` and default constructs `tp_`.

```cpp
explicit zoned_time(string_view name);
```

**7** *Constraints:* `traits::locate_zone(string_view{})` is a well-formed expression and `zoned_time` is constructible from the return type of `traits::locate_zone(string_view{})`.

**8** *Effects:* Initializes `zone_` with `traits::locate_zone(name)` and default constructs `tp_`.

```cpp
template<class Duration2>
  zoned_time(const zoned_time<Duration2, TimeZonePtr>& y);
```

**9** *Constraints:* `is_convertible_v<sys_time<Duration2>, sys_time<Duration>>` is `true`.

**10** *Effects:* Initializes `zone_` with `y.zone_` and `tp_` with `y.tp_`.

```cpp
zoned_time(TimeZonePtr z, const sys_time<Duration>& st);
```

**11** *Preconditions:* `z` refers to a time zone.

**12** *Effects:* Initializes `zone_` with `std::move(z)` and `tp_` with `st`.

```cpp
zoned_time(string_view name, const sys_time<Duration>& st);
```

**13** *Constraints:* `zoned_time` is constructible from the return type of `traits::locate_zone(name)` and `st`.

**14** *Effects:* Equivalent to construction with `{traits::locate_zone(name), st}`.

```cpp
zoned_time(TimeZonePtr z, const local_time<Duration>& tp);
```

**15** *Preconditions:* `z` refers to a time zone.

**16** *Constraints:*

```cpp
is_convertible_v<
  decltype(declval<TimeZonePtr&>()->to_sys(local_time<Duration>{})),
  sys_time<duration>>
```

is `true`.

**17** *Effects:* Initializes `zone_` with `std::move(z)` and `tp_` with `zone_->to_sys(tp)`.

```cpp
zoned_time(string_view name, const local_time<Duration>& tp);
```

**18** *Constraints:* `zoned_time` is constructible from the return type of `traits::locate_zone(name)` and `tp`.

**19** *Effects:* Equivalent to construction with `{traits::locate_zone(name), tp}`.

```cpp
zoned_time(TimeZonePtr z, const local_time<Duration>& tp, choose c);
```

**20** *Preconditions:* `z` refers to a time zone.

**21** *Constraints:*

```cpp
is_convertible_v<
  decltype(declval<TimeZonePtr&>()->to_sys(local_time<Duration>{}, choose::earliest)),
  sys_time<duration>>
```

is `true`.

**22** *Effects:* Initializes `zone_` with `std::move(z)` and `tp_` with `zone_->to_sys(tp, c)`.

```cpp
zoned_time(string_view name, const local_time<Duration>& tp, choose c);
```

**23** *Constraints:* `zoned_time` is constructible from the return type of `traits::locate_zone(name)`, `local_time<Duration>`, and `choose`.

**24** *Effects:* Equivalent to construction with `{traits::locate_zone(name), tp, c}`.

```cpp
template<class Duration2, class TimeZonePtr2>
  zoned_time(TimeZonePtr z, const zoned_time<Duration2, TimeZonePtr2>& y);
```

**25** *Constraints:* `is_convertible_v<sys_time<Duration2>, sys_time<Duration>>` is `true`.

**26** *Preconditions:* `z` refers to a valid time zone.

**27** *Effects:* Initializes `zone_` with `std::move(z)` and `tp_` with `y.tp_`.

```cpp
template<class Duration2, class TimeZonePtr2>
  zoned_time(TimeZonePtr z, const zoned_time<Duration2, TimeZonePtr2>& y, choose);
```

**28** *Constraints:* `is_convertible_v<sys_time<Duration2>, sys_time<Duration>>` is `true`.

**29** *Preconditions:* `z` refers to a valid time zone.

**30** *Effects:* Equivalent to construction with `{z, y}`.

**31** [*Note 1*: The `choose` parameter has no effect. — *end note*]

```cpp
template<class Duration2, class TimeZonePtr2>
  zoned_time(string_view name, const zoned_time<Duration2, TimeZonePtr2>& y);
```

**32** *Constraints:* `zoned_time` is constructible from the return type of `traits::locate_zone(name)` and the type `zoned_time<Duration2, TimeZonePtr2>`.

**33** *Effects:* Equivalent to construction with `{traits::locate_zone(name), y}`.

```cpp
template<class Duration2, class TimeZonePtr2>
  zoned_time(string_view name, const zoned_time<Duration2, TimeZonePtr2>& y, choose c);
```

**34** *Constraints:* `zoned_time` is constructible from the return type of `traits::locate_zone(name)`, the type `zoned_time<Duration2, TimeZonePtr2>`, and the type `choose`.

**35** *Effects:* Equivalent to construction with `{traits::locate_zone(name), y, c}`.

**36** [*Note 2*: The `choose` parameter has no effect. — *end note*]
