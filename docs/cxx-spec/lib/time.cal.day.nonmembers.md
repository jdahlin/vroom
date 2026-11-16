# 27.8.3.3 Non-member functions [time.cal.day.nonmembers]

[Contents](../README.md) · up: [27.8.3 Class `day` \[time.cal.day\]](time.cal.day.md) · prev: [27.8.3.2 Member functions \[time.cal.day.members\]](time.cal.day.members.md) · next: [27.8.4 Class `month` \[time.cal.month\]](time.cal.month.md)

```cpp
constexpr bool operator==(const day& x, const day& y) noexcept;
```

**1** *Returns:* `unsigned{x} == unsigned{y}`.

```cpp
constexpr strong_ordering operator<=>(const day& x, const day& y) noexcept;
```

**2** *Returns:* `unsigned{x} <=> unsigned{y}`.

```cpp
constexpr day operator+(const day& x, const days& y) noexcept;
```

**3** *Returns:* `day(unsigned{x} + y.count())`.

```cpp
constexpr day operator+(const days& x, const day& y) noexcept;
```

**4** *Returns:* `y + x`.

```cpp
constexpr day operator-(const day& x, const days& y) noexcept;
```

**5** *Returns:* `x + -y`.

```cpp
constexpr days operator-(const day& x, const day& y) noexcept;
```

**6** *Returns:* `days{int(unsigned{x}) - int(unsigned{y})}`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const day& d);
```

**7** *Effects:* Equivalent to:

```cpp
return os << (d.ok() ?
  format(STATICALLY-WIDEN<charT>("{:%d}"), d) :
  format(STATICALLY-WIDEN<charT>("{:%d} is not a valid day"), d));
```

```cpp
template<class charT, class traits, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                day& d, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**8** *Effects:* Attempts to parse the input stream `is` into the `day` `d` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid day, `is.setstate(ios_base::failbit)` is called and `d` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**9** *Returns:* `is`.

```cpp
constexpr chrono::day operator""d(unsigned long long d) noexcept;
```

**10** *Returns:* `day{static_cast<unsigned>(d)}`.
