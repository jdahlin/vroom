# 27.7.3.3 Non-member functions [time.clock.tai.nonmembers]

[Contents](../../README.md) · up: [27.7.3 Class `tai_clock` \[time.clock.tai\]](time.clock.tai.md) · prev: [27.7.3.2 Member functions \[time.clock.tai.members\]](time.clock.tai.members.md) · next: [27.7.4 Class `gps_clock` \[time.clock.gps\]](time.clock.gps.md)

```cpp
template<class charT, class traits, class Duration>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const tai_time<Duration>& t);
```

**1** *Effects:* Equivalent to:

```cpp
return os << format(STATICALLY-WIDEN<charT>("{:%F %T}"), t);
```

**2** [*Example 1*:

```cpp
auto st = sys_days{2000y/January/1};
auto tt = clock_cast<tai_clock>(st);
cout << format("{0:%F %T %Z} == {1:%F %T %Z}\n", st, tt);
```

Produces this output:

```text
2000-01-01 00:00:00 UTC == 2000-01-01 00:00:32 TAI
```

— *end example*]

```cpp
template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                tai_time<Duration>& tp, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**3** *Effects:* Attempts to parse the input stream `is` into the `tai_time` `tp` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid date, `is.setstate(ios_base::failbit)` is called and `tp` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null. Additionally, the parsed offset will be subtracted from the successfully parsed timestamp prior to assigning that difference to `tp`.

**4** *Returns:* `is`.
