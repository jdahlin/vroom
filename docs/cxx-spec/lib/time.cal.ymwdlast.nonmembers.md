# 27.8.17.3 Non-member functions [time.cal.ymwdlast.nonmembers]

[Contents](../README.md) · up: [27.8.17 Class `year_month_weekday_last` \[time.cal.ymwdlast\]](time.cal.ymwdlast.md) · prev: [27.8.17.2 Member functions \[time.cal.ymwdlast.members\]](time.cal.ymwdlast.members.md) · next: [27.8.18 Conventional syntax operators \[time.cal.operators\]](time.cal.operators.md)

```cpp
constexpr bool operator==(const year_month_weekday_last& x,
                          const year_month_weekday_last& y) noexcept;
```

**1** *Returns:*

```cpp
x.year() == y.year() && x.month() == y.month() && x.weekday_last() == y.weekday_last()
```

```cpp
constexpr year_month_weekday_last
  operator+(const year_month_weekday_last& ymwdl, const months& dm) noexcept;
```

**2** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**3** *Returns:* `(ymwdl.year() / ymwdl.month() + dm) / ymwdl.weekday_last()`.

```cpp
constexpr year_month_weekday_last
  operator+(const months& dm, const year_month_weekday_last& ymwdl) noexcept;
```

**4** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**5** *Returns:* `ymwdl + dm`.

```cpp
constexpr year_month_weekday_last
  operator-(const year_month_weekday_last& ymwdl, const months& dm) noexcept;
```

**6** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**7** *Returns:* `ymwdl + (-dm)`.

```cpp
constexpr year_month_weekday_last
  operator+(const year_month_weekday_last& ymwdl, const years& dy) noexcept;
```

**8** *Returns:* `{ymwdl.year()+dy, ymwdl.month(), ymwdl.weekday_last()}`.

```cpp
constexpr year_month_weekday_last
  operator+(const years& dy, const year_month_weekday_last& ymwdl) noexcept;
```

**9** *Returns:* `ymwdl + dy`.

```cpp
constexpr year_month_weekday_last
  operator-(const year_month_weekday_last& ymwdl, const years& dy) noexcept;
```

**10** *Returns:* `ymwdl + (-dy)`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const year_month_weekday_last& ymwdl);
```

**11** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}/{}"),
                    ymwdl.year(), ymwdl.month(), ymwdl.weekday_last());
```
