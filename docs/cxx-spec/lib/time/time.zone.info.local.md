# 27.11.4.2 Class `local_info` [time.zone.info.local]

[Contents](../../README.md) · up: [27.11.4 Information classes \[time.zone.info\]](time.zone.info.md) · prev: [27.11.4.1 Class `sys_info` \[time.zone.info.sys\]](time.zone.info.sys.md) · next: [27.11.5 Class `time_zone` \[time.zone.timezone\]](time.zone.timezone.md)

```cpp
namespace std::chrono {
  struct local_info {
    static constexpr int unique      = 0;
    static constexpr int nonexistent = 1;
    static constexpr int ambiguous   = 2;

    int result;
    sys_info first;
    sys_info second;
  };
}
```

**1** [*Note 1*: This type provides a low-level interface to time zone information. Typical conversions from `local_time` to `sys_time` will use this class implicitly, not explicitly. — *end note*]

**2** Describes the result of converting a `local_time` to a `sys_time` as follows:

- When a `local_time` to `sys_time` conversion is unique, `result == unique`, `first` will be filled out with the correct `sys_info`, and `second` will be zero-initialized.
- If the conversion stems from a nonexistent `local_time` then `result == nonexistent`, `first` will be filled out with the `sys_info` that ends just prior to the `local_time`, and `second` will be filled out with the `sys_info` that begins just after the `local_time`.
- If the conversion stems from an ambiguous `local_time`, then `result == ambiguous`, `first` will be filled out with the `sys_info` that ends just after the `local_time`, and `second` will be filled out with the `sys_info` that starts just before the `local_time`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const local_info& r);
```

**3** *Effects:* Streams out the `local_info` object `r` in an unspecified format.

**4** *Returns:* `os`.
