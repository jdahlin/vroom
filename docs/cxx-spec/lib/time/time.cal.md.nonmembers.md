# 27.8.9.3 Non-member functions [time.cal.md.nonmembers]

[Contents](../../README.md) · up: [27.8.9 Class `month_day` \[time.cal.md\]](time.cal.md.md) · prev: [27.8.9.2 Member functions \[time.cal.md.members\]](time.cal.md.members.md) · next: [27.8.10 Class `month_day_last` \[time.cal.mdlast\]](time.cal.mdlast.md)

```cpp
constexpr bool operator==(const month_day& x, const month_day& y) noexcept;
```

**1** *Returns:* `x.month() == y.month() && x.day() == y.day()`.

```cpp
constexpr strong_ordering operator<=>(const month_day& x, const month_day& y) noexcept;
```

**2** *Effects:* Equivalent to:

```cpp
if (auto c = x.month() <=> y.month(); c != 0) return c;
return x.day() <=> y.day();
```

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const month_day& md);
```

**3** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}"),
                    md.month(), md.day());
```

```cpp
template<class charT, class traits, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                month_day& md, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**4** *Effects:* Attempts to parse the input stream `is` into the `month_day` `md` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid `month_day`, `is.setstate(ios_base::failbit)` is called and `md` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**5** *Returns:* `is`.
