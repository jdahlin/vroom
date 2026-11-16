# 27.8.18 Conventional syntax operators [time.cal.operators]

[Contents](../../README.md) · up: [27.8 The civil calendar \[time.cal\]](time.cal.md) · prev: [27.8.17.3 Non-member functions \[time.cal.ymwdlast.nonmembers\]](time.cal.ymwdlast.nonmembers.md) · next: [27.9 Class template `hh_mm_ss` \[time.hms\]](time.hms.md)

**1** A set of overloaded `operator/` functions provides a conventional syntax for the creation of civil calendar dates.

**2** [*Note 1*: The year, month, and day are accepted in any of the following 3 orders:

```cpp
year/month/day
month/day/year
day/month/year
```

Anywhere a `day` is required, any of the following can also be specified:

```cpp
last
weekday[i]
weekday[last]
```

— *end note*]

**3** [*Note 2*: Partial-date types such as `year_month` and `month_day` can be created by not applying the second division operator for any of the three orders. For example:

```cpp
year_month ym = 2015y/April;
month_day md1 = April/4;
month_day md2 = 4d/April;
```

— *end note*]

**4** [*Example 1*:

```cpp
auto a = 2015/4/4;          // a == int(125)
auto b = 2015y/4/4;         // b == year_month_day{year(2015), month(4), day(4)}
auto c = 2015y/4d/April;    // error: no viable operator/ for first /
auto d = 2015/April/4;      // error: no viable operator/ for first /
```

— *end example*]

```cpp
constexpr year_month
  operator/(const year& y, const month& m) noexcept;
```

**5** *Returns:* `{y, m}`.

```cpp
constexpr year_month
  operator/(const year& y, int   m) noexcept;
```

**6** *Returns:* `y / month(m)`.

```cpp
constexpr month_day
  operator/(const month& m, const day& d) noexcept;
```

**7** *Returns:* `{m, d}`.

```cpp
constexpr month_day
  operator/(const month& m, int d) noexcept;
```

**8** *Returns:* `m / day(d)`.

```cpp
constexpr month_day
  operator/(int m, const day& d) noexcept;
```

**9** *Returns:* `month(m) / d`.

```cpp
constexpr month_day
  operator/(const day& d, const month& m) noexcept;
```

**10** *Returns:* `m / d`.

```cpp
constexpr month_day
  operator/(const day& d, int m) noexcept;
```

**11** *Returns:* `month(m) / d`.

```cpp
constexpr month_day_last
  operator/(const month& m, last_spec) noexcept;
```

**12** *Returns:* `month_day_last{m}`.

```cpp
constexpr month_day_last
  operator/(int m, last_spec) noexcept;
```

**13** *Returns:* `month(m) / last`.

```cpp
constexpr month_day_last
  operator/(last_spec, const month& m) noexcept;
```

**14** *Returns:* `m / last`.

```cpp
constexpr month_day_last
  operator/(last_spec, int m) noexcept;
```

**15** *Returns:* `month(m) / last`.

```cpp
constexpr month_weekday
  operator/(const month& m, const weekday_indexed& wdi) noexcept;
```

**16** *Returns:* `{m, wdi}`.

```cpp
constexpr month_weekday
  operator/(int m, const weekday_indexed& wdi) noexcept;
```

**17** *Returns:* `month(m) / wdi`.

```cpp
constexpr month_weekday
  operator/(const weekday_indexed& wdi, const month& m) noexcept;
```

**18** *Returns:* `m / wdi`.

```cpp
constexpr month_weekday
  operator/(const weekday_indexed& wdi, int m) noexcept;
```

**19** *Returns:* `month(m) / wdi`.

```cpp
constexpr month_weekday_last
  operator/(const month& m, const weekday_last& wdl) noexcept;
```

**20** *Returns:* `{m, wdl}`.

```cpp
constexpr month_weekday_last
  operator/(int m, const weekday_last& wdl) noexcept;
```

**21** *Returns:* `month(m) / wdl`.

```cpp
constexpr month_weekday_last
  operator/(const weekday_last& wdl, const month& m) noexcept;
```

**22** *Returns:* `m / wdl`.

```cpp
constexpr month_weekday_last
  operator/(const weekday_last& wdl, int m) noexcept;
```

**23** *Returns:* `month(m) / wdl`.

```cpp
constexpr year_month_day
  operator/(const year_month& ym, const day& d) noexcept;
```

**24** *Returns:* `{ym.year(), ym.month(), d}`.

```cpp
constexpr year_month_day
  operator/(const year_month& ym, int d) noexcept;
```

**25** *Returns:* `ym / day(d)`.

```cpp
constexpr year_month_day
  operator/(const year& y, const month_day& md) noexcept;
```

**26** *Returns:* `y / md.month() / md.day()`.

```cpp
constexpr year_month_day
  operator/(int y, const month_day& md) noexcept;
```

**27** *Returns:* `year(y) / md`.

```cpp
constexpr year_month_day
  operator/(const month_day& md, const year& y) noexcept;
```

**28** *Returns:* `y / md`.

```cpp
constexpr year_month_day
  operator/(const month_day& md, int y) noexcept;
```

**29** *Returns:* `year(y) / md`.

```cpp
constexpr year_month_day_last
  operator/(const year_month& ym, last_spec) noexcept;
```

**30** *Returns:* `{ym.year(), month_day_last{ym.month()}}`.

```cpp
constexpr year_month_day_last
  operator/(const year& y, const month_day_last& mdl) noexcept;
```

**31** *Returns:* `{y, mdl}`.

```cpp
constexpr year_month_day_last
  operator/(int y, const month_day_last& mdl) noexcept;
```

**32** *Returns:* `year(y) / mdl`.

```cpp
constexpr year_month_day_last
  operator/(const month_day_last& mdl, const year& y) noexcept;
```

**33** *Returns:* `y / mdl`.

```cpp
constexpr year_month_day_last
  operator/(const month_day_last& mdl, int y) noexcept;
```

**34** *Returns:* `year(y) / mdl`.

```cpp
constexpr year_month_weekday
  operator/(const year_month& ym, const weekday_indexed& wdi) noexcept;
```

**35** *Returns:* `{ym.year(), ym.month(), wdi}`.

```cpp
constexpr year_month_weekday
  operator/(const year& y, const month_weekday& mwd) noexcept;
```

**36** *Returns:* `{y, mwd.month(), mwd.weekday_indexed()}`.

```cpp
constexpr year_month_weekday
  operator/(int y, const month_weekday& mwd) noexcept;
```

**37** *Returns:* `year(y) / mwd`.

```cpp
constexpr year_month_weekday
  operator/(const month_weekday& mwd, const year& y) noexcept;
```

**38** *Returns:* `y / mwd`.

```cpp
constexpr year_month_weekday
  operator/(const month_weekday& mwd, int y) noexcept;
```

**39** *Returns:* `year(y) / mwd`.

```cpp
constexpr year_month_weekday_last
  operator/(const year_month& ym, const weekday_last& wdl) noexcept;
```

**40** *Returns:* `{ym.year(), ym.month(), wdl}`.

```cpp
constexpr year_month_weekday_last
  operator/(const year& y, const month_weekday_last& mwdl) noexcept;
```

**41** *Returns:* `{y, mwdl.month(), mwdl.weekday_last()}`.

```cpp
constexpr year_month_weekday_last
  operator/(int y, const month_weekday_last& mwdl) noexcept;
```

**42** *Returns:* `year(y) / mwdl`.

```cpp
constexpr year_month_weekday_last
  operator/(const month_weekday_last& mwdl, const year& y) noexcept;
```

**43** *Returns:* `y / mwdl`.

```cpp
constexpr year_month_weekday_last
  operator/(const month_weekday_last& mwdl, int y) noexcept;
```

**44** *Returns:* `year(y) / mwdl`.
