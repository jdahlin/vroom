# 27.8.13.3 Non-member functions [time.cal.ym.nonmembers]

[Contents](../README.md) · up: [27.8.13 Class `year_month` \[time.cal.ym\]](time.cal.ym.md) · prev: [27.8.13.2 Member functions \[time.cal.ym.members\]](time.cal.ym.members.md) · next: [27.8.14 Class `year_month_day` \[time.cal.ymd\]](time.cal.ymd.md)

```cpp
constexpr bool operator==(const year_month& x, const year_month& y) noexcept;
```

**1** *Returns:* `x.year() == y.year() && x.month() == y.month()`.

```cpp
constexpr strong_ordering operator<=>(const year_month& x, const year_month& y) noexcept;
```

**2** *Effects:* Equivalent to:

```cpp
if (auto c = x.year() <=> y.year(); c != 0) return c;
return x.month() <=> y.month();
```

```cpp
constexpr year_month operator+(const year_month& ym, const months& dm) noexcept;
```

**3** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**4** *Returns:* A `year_month` value `z` such that `z.ok() && z - ym == dm` is `true`.

**5** *Complexity:* 𝑂(1) with respect to the value of `dm`.

```cpp
constexpr year_month operator+(const months& dm, const year_month& ym) noexcept;
```

**6** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**7** *Returns:* `ym + dm`.

```cpp
constexpr year_month operator-(const year_month& ym, const months& dm) noexcept;
```

**8** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**9** *Returns:* `ym + -dm`.

```cpp
constexpr months operator-(const year_month& x, const year_month& y) noexcept;
```

**10** *Returns:*

```cpp
x.year() - y.year() + months{static_cast<int>(unsigned{x.month()}) -
                             static_cast<int>(unsigned{y.month()})}
```

```cpp
constexpr year_month operator+(const year_month& ym, const years& dy) noexcept;
```

**11** *Returns:* `(ym.year() + dy) / ym.month()`.

```cpp
constexpr year_month operator+(const years& dy, const year_month& ym) noexcept;
```

**12** *Returns:* `ym + dy`.

```cpp
constexpr year_month operator-(const year_month& ym, const years& dy) noexcept;
```

**13** *Returns:* `ym + -dy`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const year_month& ym);
```

**14** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}"),
                    ym.year(), ym.month());
```

```cpp
template<class charT, class traits, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                year_month& ym, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**15** *Effects:* Attempts to parse the input stream `is` into the `year_month` `ym` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid `year_month`, `is.setstate(ios_base::failbit)` is called and `ym` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**16** *Returns:* `is`.
