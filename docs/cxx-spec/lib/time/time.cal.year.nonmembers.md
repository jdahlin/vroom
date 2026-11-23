# 27.8.5.3 Non-member functions [time.cal.year.nonmembers]

[Contents](../../README.md) · up: [27.8.5 Class `year` \[time.cal.year\]](time.cal.year.md) · prev: [27.8.5.2 Member functions \[time.cal.year.members\]](time.cal.year.members.md) · next: [27.8.6 Class `weekday` \[time.cal.wd\]](time.cal.wd.md)

```cpp
constexpr bool operator==(const year& x, const year& y) noexcept;
```

**1** *Returns:* `int{x} == int{y}`.

```cpp
constexpr strong_ordering operator<=>(const year& x, const year& y) noexcept;
```

**2** *Returns:* `int{x} <=> int{y}`.

```cpp
constexpr year operator+(const year& x, const years& y) noexcept;
```

**3** *Returns:* `year{int{x} + y.count()}`.

```cpp
constexpr year operator+(const years& x, const year& y) noexcept;
```

**4** *Returns:* `y + x`.

```cpp
constexpr year operator-(const year& x, const years& y) noexcept;
```

**5** *Returns:* `x + -y`.

```cpp
constexpr years operator-(const year& x, const year& y) noexcept;
```

**6** *Returns:* `years{int{x} - int{y}}`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const year& y);
```

**7** *Effects:* Equivalent to:

```cpp
return os << (y.ok() ?
  format(STATICALLY-WIDEN<charT>("{:%Y}"), y) :
  format(STATICALLY-WIDEN<charT>("{:%Y} is not a valid year"), y));
```

```cpp
template<class charT, class traits, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                year& y, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**8** *Effects:* Attempts to parse the input stream `is` into the `year` `y` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid year, `is.setstate(ios_base::failbit)` is called and `y` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**9** *Returns:* `is`.

```cpp
constexpr chrono::year operator""y(unsigned long long y) noexcept;
```

**10** *Returns:* `year{static_cast<int>(y)}`.
