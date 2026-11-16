# 27.7.5.3 Non-member functions [time.clock.file.nonmembers]

[Contents](../README.md) · up: [27.7.5 Type `file_clock` \[time.clock.file\]](time.clock.file.md) · prev: [27.7.5.2 Member functions \[time.clock.file.members\]](time.clock.file.members.md) · next: [27.7.6 Class `steady_clock` \[time.clock.steady\]](time.clock.steady.md)

```cpp
template<class charT, class traits, class Duration>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const file_time<Duration>& t);
```

**1** *Effects:* Equivalent to:

```cpp
return os << format(STATICALLY-WIDEN<charT>("{:%F %T}"), t);
```

```cpp
template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                file_time<Duration>& tp, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**2** *Effects:* Attempts to parse the input stream `is` into the `file_time` `tp` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid date, `is.setstate(ios_base::failbit)` is called and `tp` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null. Additionally, the parsed offset will be subtracted from the successfully parsed timestamp prior to assigning that difference to `tp`.

**3** *Returns:* `is`.
