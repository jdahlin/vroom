# 27.8.16.3 Non-member functions [time.cal.ymwd.nonmembers]

[Contents](../README.md) · up: [27.8.16 Class `year_month_weekday` \[time.cal.ymwd\]](time.cal.ymwd.md) · prev: [27.8.16.2 Member functions \[time.cal.ymwd.members\]](time.cal.ymwd.members.md) · next: [27.8.17 Class `year_month_weekday_last` \[time.cal.ymwdlast\]](time.cal.ymwdlast.md)

```cpp
constexpr bool operator==(const year_month_weekday& x, const year_month_weekday& y) noexcept;
```

**1** *Returns:*

```cpp
x.year() == y.year() && x.month() == y.month() && x.weekday_indexed() == y.weekday_indexed()
```

```cpp
constexpr year_month_weekday operator+(const year_month_weekday& ymwd, const months& dm) noexcept;
```

**2** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**3** *Returns:* `(ymwd.year() / ymwd.month() + dm) / ymwd.weekday_indexed()`.

```cpp
constexpr year_month_weekday operator+(const months& dm, const year_month_weekday& ymwd) noexcept;
```

**4** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**5** *Returns:* `ymwd + dm`.

```cpp
constexpr year_month_weekday operator-(const year_month_weekday& ymwd, const months& dm) noexcept;
```

**6** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**7** *Returns:* `ymwd + (-dm)`.

```cpp
constexpr year_month_weekday operator+(const year_month_weekday& ymwd, const years& dy) noexcept;
```

**8** *Returns:* `{ymwd.year()+dy, ymwd.month(), ymwd.weekday_indexed()}`.

```cpp
constexpr year_month_weekday operator+(const years& dy, const year_month_weekday& ymwd) noexcept;
```

**9** *Returns:* `ymwd + dy`.

```cpp
constexpr year_month_weekday operator-(const year_month_weekday& ymwd, const years& dy) noexcept;
```

**10** *Returns:* `ymwd + (-dy)`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const year_month_weekday& ymwd);
```

**11** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}/{}"),
                    ymwd.year(), ymwd.month(), ymwd.weekday_indexed());
```
