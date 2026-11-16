# 27.7.8 Local time [time.clock.local]

[Contents](../README.md) · up: [27.7 Clocks \[time.clock\]](time.clock.md) · prev: [27.7.7 Class `high_resolution_clock` \[time.clock.hires\]](time.clock.hires.md) · next: [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md)

**1** The family of time points denoted by `local_time<Duration>` are based on the pseudo clock `local_t`. `local_t` has no member `now()` and thus does not meet the clock requirements. Nevertheless `local_time<Duration>` serves the vital role of representing local time with respect to a not-yet-specified time zone. Aside from being able to get the current time, the complete `time_point` algebra is available for `local_time<Duration>` (just as for `sys_time<Duration>`).

```cpp
template<class charT, class traits, class Duration>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const local_time<Duration>& lt);
```

**2** *Effects:*

```cpp
os << sys_time<Duration>{lt.time_since_epoch()};
```

**3** *Returns:* `os`.

```cpp
template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                local_time<Duration>& tp, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**4** *Effects:* Attempts to parse the input stream `is` into the `local_time` `tp` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid date, `is.setstate(ios_base::failbit)` is called and `tp` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**5** *Returns:* `is`.
