# 27.11.5.2 Member functions [time.zone.members]

[Contents](../../README.md) · up: [27.11.5 Class `time_zone` \[time.zone.timezone\]](time.zone.timezone.md) · prev: [27.11.5.1 Overview \[time.zone.overview\]](time.zone.overview.md) · next: [27.11.5.3 Non-member functions \[time.zone.nonmembers\]](time.zone.nonmembers.md)

```cpp
string_view name() const noexcept;
```

**1** *Returns:* The name of the `time_zone`.

**2** [*Example 1*: `"America/New_York"`. — *end example*]

```cpp
template<class Duration>
  sys_info get_info(const sys_time<Duration>& st) const;
```

**3** *Returns:* A `sys_info` `i` for which `st` is in the range `[i.begin, i.end)`.

```cpp
template<class Duration>
  local_info get_info(const local_time<Duration>& tp) const;
```

**4** *Returns:* A `local_info` for `tp`.

```cpp
template<class Duration>
  sys_time<common_type_t<Duration, seconds>>
    to_sys(const local_time<Duration>& tp) const;
```

**5** *Returns:* A `sys_time` that is at least as fine as `seconds`, and will be finer if the argument `tp` has finer precision. This `sys_time` is the UTC equivalent of `tp` according to the rules of this `time_zone`.

**6** *Throws:* If the conversion from `tp` to a `sys_time` is ambiguous, throws `ambiguous_local_time`. If the `tp` represents a non-existent time between two UTC `time_points`, throws `nonexistent_local_time`.

```cpp
template<class Duration>
  sys_time<common_type_t<Duration, seconds>>
    to_sys(const local_time<Duration>& tp, choose z) const;
```

**7** *Returns:* A `sys_time` that is at least as fine as `seconds`, and will be finer if the argument `tp` has finer precision. This `sys_time` is the UTC equivalent of `tp` according to the rules of this `time_zone`. If the conversion from `tp` to a `sys_time` is ambiguous, returns the earlier `sys_time` if `z == choose::earliest`, and returns the later `sys_time` if `z == choose::latest`. If the `tp` represents a non-existent time between two UTC `time_points`, then the two UTC `time_points` will be the same, and that UTC `time_point` will be returned.

```cpp
template<class Duration>
  local_time<common_type_t<Duration, seconds>>
    to_local(const sys_time<Duration>& tp) const;
```

**8** *Returns:* The `local_time` associated with `tp` and this `time_zone`.
