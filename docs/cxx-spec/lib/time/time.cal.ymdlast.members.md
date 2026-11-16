# 27.8.15.2 Member functions [time.cal.ymdlast.members]

[Contents](../../README.md) · up: [27.8.15 Class `year_month_day_last` \[time.cal.ymdlast\]](time.cal.ymdlast.md) · prev: [27.8.15.1 Overview \[time.cal.ymdlast.overview\]](time.cal.ymdlast.overview.md) · next: [27.8.15.3 Non-member functions \[time.cal.ymdlast.nonmembers\]](time.cal.ymdlast.nonmembers.md)

```cpp
constexpr year_month_day_last(const chrono::year& y,
                              const chrono::month_day_last& mdl) noexcept;
```

**1** *Effects:* Initializes `y_` with `y` and `mdl_` with `mdl`.

```cpp
constexpr year_month_day_last& operator+=(const months& m) noexcept;
```

**2** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**3** *Effects:* `*this = *this + m`.

**4** *Returns:* `*this`.

```cpp
constexpr year_month_day_last& operator-=(const months& m) noexcept;
```

**5** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**6** *Effects:* `*this = *this - m`.

**7** *Returns:* `*this`.

```cpp
constexpr year_month_day_last& operator+=(const years& y) noexcept;
```

**8** *Effects:* `*this = *this + y`.

**9** *Returns:* `*this`.

```cpp
constexpr year_month_day_last& operator-=(const years& y) noexcept;
```

**10** *Effects:* `*this = *this - y`.

**11** *Returns:* `*this`.

```cpp
constexpr chrono::year year() const noexcept;
```

**12** *Returns:* `y_`.

```cpp
constexpr chrono::month month() const noexcept;
```

**13** *Returns:* `mdl_.month()`.

```cpp
constexpr chrono::month_day_last month_day_last() const noexcept;
```

**14** *Returns:* `mdl_`.

```cpp
constexpr chrono::day day() const noexcept;
```

**15** *Returns:* If `ok()` is `true`, returns a `day` representing the last day of the (`year`, `month`) pair represented by `*this`. Otherwise, the returned value is unspecified.

**16** [*Note 1*: This value may be computed on demand. — *end note*]

```cpp
constexpr operator sys_days() const noexcept;
```

**17** *Returns:* `sys_days{year()/month()/day()}`.

```cpp
constexpr explicit operator local_days() const noexcept;
```

**18** *Returns:* `local_days{sys_days{*this}.time_since_epoch()}`.

```cpp
constexpr bool ok() const noexcept;
```

**19** *Returns:* `y_.ok() && mdl_.ok()`.
