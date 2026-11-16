# 27.7.1.3 Non-member functions [time.clock.system.nonmembers]

[Contents](../README.md) · up: [27.7.1 Class `system_clock` \[time.clock.system\]](time.clock.system.md) · prev: [27.7.1.2 Members \[time.clock.system.members\]](time.clock.system.members.md) · next: [27.7.2 Class `utc_clock` \[time.clock.utc\]](time.clock.utc.md)

```cpp
template<class charT, class traits, class Duration>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const sys_time<Duration>& tp);
```

**1** *Constraints:* `treat_as_floating_point_v<typename Duration::rep>` is `false`, and `Duration{1} < days{1}` is `true`.

**2** *Effects:* Equivalent to:

```cpp
auto const dp = floor<days>(tp);
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{} {}"),
                    year_month_day{dp}, hh_mm_ss{tp-dp});
```

**3** [*Example 1*:

```cpp
cout << sys_seconds{0s} << '\n';                // 1970-01-01 00:00:00
cout << sys_seconds{946'684'800s} << '\n';      // 2000-01-01 00:00:00
cout << sys_seconds{946'688'523s} << '\n';      // 2000-01-01 01:02:03
```

— *end example*]

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const sys_days& dp);
```

**4** *Effects:* `os << year_month_day{dp}`.

**5** *Returns:* `os`.

```cpp
template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                sys_time<Duration>& tp, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**6** *Effects:* Attempts to parse the input stream `is` into the `sys_time` `tp` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid date, `is.setstate(ios_base::failbit)` is called and `tp` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null. Additionally, the parsed offset will be subtracted from the successfully parsed timestamp prior to assigning that difference to `tp`.

**7** *Returns:* `is`.
