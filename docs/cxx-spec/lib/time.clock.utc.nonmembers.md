# 27.7.2.3 Non-member functions [time.clock.utc.nonmembers]

[Contents](../README.md) · up: [27.7.2 Class `utc_clock` \[time.clock.utc\]](time.clock.utc.md) · prev: [27.7.2.2 Member functions \[time.clock.utc.members\]](time.clock.utc.members.md) · next: [27.7.3 Class `tai_clock` \[time.clock.tai\]](time.clock.tai.md)

```cpp
template<class charT, class traits, class Duration>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const utc_time<Duration>& t);
```

**1** *Effects:* Equivalent to:

```cpp
return os << format(STATICALLY-WIDEN<charT>("{:%F %T}"), t);
```

**2** [*Example 1*:

```cpp
auto t = sys_days{July/1/2015} - 500ms;
auto u = clock_cast<utc_clock>(t);
for (auto i = 0; i < 8; ++i, u += 250ms)
  cout << u << " UTC\n";
```

Produces this output:

```text
2015-06-30 23:59:59.500 UTC
2015-06-30 23:59:59.750 UTC
2015-06-30 23:59:60.000 UTC
2015-06-30 23:59:60.250 UTC
2015-06-30 23:59:60.500 UTC
2015-06-30 23:59:60.750 UTC
2015-07-01 00:00:00.000 UTC
2015-07-01 00:00:00.250 UTC
```

— *end example*]

```cpp
template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                utc_time<Duration>& tp, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**3** *Effects:* Attempts to parse the input stream `is` into the `utc_time` `tp` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid date, `is.setstate(ios_base::failbit)` is called and `tp` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null. Additionally, the parsed offset will be subtracted from the successfully parsed timestamp prior to assigning that difference to `tp`.

**4** *Returns:* `is`.

```cpp
struct leap_second_info {
  bool    is_leap_second;
  seconds elapsed;
};
```

**5** The type `leap_second_info` has data members and special members specified above. It has no base classes or members other than those specified.

```cpp
template<class Duration>
  leap_second_info get_leap_second_info(const utc_time<Duration>& ut);
```

**6** *Returns:* A `leap_second_info` `lsi`, where `lsi.is_leap_second` is `true` if `ut` is during a positive leap second insertion, and otherwise `false`. `lsi.elapsed` is the sum of leap seconds between 1970-01-01 and `ut`. If `lsi.is_leap_second` is `true`, the leap second referred to by `ut` is included in the sum.
