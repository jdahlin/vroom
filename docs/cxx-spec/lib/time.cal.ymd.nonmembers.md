# 27.8.14.3 Non-member functions [time.cal.ymd.nonmembers]

[Contents](../README.md) · up: [27.8.14 Class `year_month_day` \[time.cal.ymd\]](time.cal.ymd.md) · prev: [27.8.14.2 Member functions \[time.cal.ymd.members\]](time.cal.ymd.members.md) · next: [27.8.15 Class `year_month_day_last` \[time.cal.ymdlast\]](time.cal.ymdlast.md)

```cpp
constexpr bool operator==(const year_month_day& x, const year_month_day& y) noexcept;
```

**1** *Returns:* `x.year() == y.year() && x.month() == y.month() && x.day() == y.day()`.

```cpp
constexpr strong_ordering operator<=>(const year_month_day& x, const year_month_day& y) noexcept;
```

**2** *Effects:* Equivalent to:

```cpp
if (auto c = x.year() <=> y.year(); c != 0) return c;
if (auto c = x.month() <=> y.month(); c != 0) return c;
return x.day() <=> y.day();
```

```cpp
constexpr year_month_day operator+(const year_month_day& ymd, const months& dm) noexcept;
```

**3** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**4** *Returns:* `(ymd.year() / ymd.month() + dm) / ymd.day()`.

**5** [*Note 1*: If `ymd.day()` is in the range `[1d, 28d]`, `ok()` will return `true` for the resultant `year_month_day`. — *end note*]

```cpp
constexpr year_month_day operator+(const months& dm, const year_month_day& ymd) noexcept;
```

**6** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**7** *Returns:* `ymd + dm`.

```cpp
constexpr year_month_day operator-(const year_month_day& ymd, const months& dm) noexcept;
```

**8** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**9** *Returns:* `ymd + (-dm)`.

```cpp
constexpr year_month_day operator+(const year_month_day& ymd, const years& dy) noexcept;
```

**10** *Returns:* `(ymd.year() + dy) / ymd.month() / ymd.day()`.

**11** [*Note 2*: If `ymd.month()` is February and `ymd.day()` is not in the range `[1d, 28d]`, `ok()` may return `false` for the resultant `year_month_day`. — *end note*]

```cpp
constexpr year_month_day operator+(const years& dy, const year_month_day& ymd) noexcept;
```

**12** *Returns:* `ymd + dy`.

```cpp
constexpr year_month_day operator-(const year_month_day& ymd, const years& dy) noexcept;
```

**13** *Returns:* `ymd + (-dy)`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const year_month_day& ymd);
```

**14** *Effects:* Equivalent to:

```cpp
return os << (ymd.ok() ?
  format(STATICALLY-WIDEN<charT>("{:%F}"), ymd) :
  format(STATICALLY-WIDEN<charT>("{:%F} is not a valid date"), ymd));
```

```cpp
template<class charT, class traits, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                year_month_day& ymd, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**15** *Effects:* Attempts to parse the input stream `is` into the `year_month_day` `ymd` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid `year_month_day`, `is.setstate(ios_base::failbit)` is called and `ymd` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**16** *Returns:* `is`.
