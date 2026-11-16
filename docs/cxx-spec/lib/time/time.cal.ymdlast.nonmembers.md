# 27.8.15.3 Non-member functions [time.cal.ymdlast.nonmembers]

[Contents](../../README.md) · up: [27.8.15 Class `year_month_day_last` \[time.cal.ymdlast\]](time.cal.ymdlast.md) · prev: [27.8.15.2 Member functions \[time.cal.ymdlast.members\]](time.cal.ymdlast.members.md) · next: [27.8.16 Class `year_month_weekday` \[time.cal.ymwd\]](time.cal.ymwd.md)

```cpp
constexpr bool operator==(const year_month_day_last& x, const year_month_day_last& y) noexcept;
```

**1** *Returns:* `x.year() == y.year() && x.month_day_last() == y.month_day_last()`.

```cpp
constexpr strong_ordering operator<=>(const year_month_day_last& x,
                                      const year_month_day_last& y) noexcept;
```

**2** *Effects:* Equivalent to:

```cpp
if (auto c = x.year() <=> y.year(); c != 0) return c;
return x.month_day_last() <=> y.month_day_last();
```

```cpp
constexpr year_month_day_last
  operator+(const year_month_day_last& ymdl, const months& dm) noexcept;
```

**3** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**4** *Returns:* `(ymdl.year() / ymdl.month() + dm) / last`.

```cpp
constexpr year_month_day_last
  operator+(const months& dm, const year_month_day_last& ymdl) noexcept;
```

**5** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**6** *Returns:* `ymdl + dm`.

```cpp
constexpr year_month_day_last
  operator-(const year_month_day_last& ymdl, const months& dm) noexcept;
```

**7** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**8** *Returns:* `ymdl + (-dm)`.

```cpp
constexpr year_month_day_last
  operator+(const year_month_day_last& ymdl, const years& dy) noexcept;
```

**9** *Returns:* `{ymdl.year()+dy, ymdl.month_day_last()}`.

```cpp
constexpr year_month_day_last
  operator+(const years& dy, const year_month_day_last& ymdl) noexcept;
```

**10** *Returns:* `ymdl + dy`.

```cpp
constexpr year_month_day_last
  operator-(const year_month_day_last& ymdl, const years& dy) noexcept;
```

**11** *Returns:* `ymdl + (-dy)`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const year_month_day_last& ymdl);
```

**12** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}"),
                    ymdl.year(), ymdl.month_day_last());
```
