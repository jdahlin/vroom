# 27.8.14.2 Member functions [time.cal.ymd.members]

[Contents](../README.md) · up: [27.8.14 Class `year_month_day` \[time.cal.ymd\]](time.cal.ymd.md) · prev: [27.8.14.1 Overview \[time.cal.ymd.overview\]](time.cal.ymd.overview.md) · next: [27.8.14.3 Non-member functions \[time.cal.ymd.nonmembers\]](time.cal.ymd.nonmembers.md)

```cpp
constexpr year_month_day(const chrono::year& y, const chrono::month& m,
                         const chrono::day& d) noexcept;
```

**1** *Effects:* Initializes `y_` with `y`, `m_` with `m`, and `d_` with `d`.

```cpp
constexpr year_month_day(const year_month_day_last& ymdl) noexcept;
```

**2** *Effects:* Initializes `y_` with `ymdl.year()`, `m_` with `ymdl.month()`, and `d_` with `ymdl.day()`. [*Note 1*: This conversion from `year_month_day_last` to `year_month_day` may be more efficient than converting a `year_month_day_last` to a `sys_days`, and then converting that `sys_days` to a `year_month_day`. — *end note*]

```cpp
constexpr year_month_day(const sys_days& dp) noexcept;
```

**3** *Effects:* Constructs an object of type `year_month_day` that corresponds to the date represented by `dp`.

**4** *Remarks:* For any value `ymd` of type `year_month_day` for which `ymd.ok()` is `true`, `ymd == year_month_day{sys_days{ymd}}` is `true`.

```cpp
constexpr explicit year_month_day(const local_days& dp) noexcept;
```

**5** *Effects:* Equivalent to constructing with `sys_days{dp.time_since_epoch()}`.

```cpp
constexpr year_month_day& operator+=(const months& m) noexcept;
```

**6** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**7** *Effects:* `*this = *this + m`.

**8** *Returns:* `*this`.

```cpp
constexpr year_month_day& operator-=(const months& m) noexcept;
```

**9** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**10** *Effects:* `*this = *this - m`.

**11** *Returns:* `*this`.

```cpp
constexpr year_month_day& year_month_day::operator+=(const years& y) noexcept;
```

**12** *Effects:* `*this = *this + y`.

**13** *Returns:* `*this`.

```cpp
constexpr year_month_day& year_month_day::operator-=(const years& y) noexcept;
```

**14** *Effects:* `*this = *this - y`.

**15** *Returns:* `*this`.

```cpp
constexpr chrono::year year() const noexcept;
```

**16** *Returns:* `y_`.

```cpp
constexpr chrono::month month() const noexcept;
```

**17** *Returns:* `m_`.

```cpp
constexpr chrono::day day() const noexcept;
```

**18** *Returns:* `d_`.

```cpp
constexpr operator sys_days() const noexcept;
```

**19** *Returns:* If `ok()`, returns a `sys_days` holding a count of days from the `sys_days` epoch to `*this` (a negative value if `*this` represents a date prior to the `sys_days` epoch). Otherwise, if `y_.ok() && m_.ok()` is `true`, returns `sys_days{y_/m_/1d} + (d_ - 1d)`. Otherwise the value returned is unspecified.

**20** *Remarks:* A `sys_days` in the range `[days{-12687428}, days{11248737}]` which is converted to a `year_month_day` has the same value when converted back to a `sys_days`.

**21** [*Example 1*:

```cpp
static_assert(year_month_day{sys_days{2017y/January/0}}  == 2016y/December/31);
static_assert(year_month_day{sys_days{2017y/January/31}} == 2017y/January/31);
static_assert(year_month_day{sys_days{2017y/January/32}} == 2017y/February/1);
```

— *end example*]

```cpp
constexpr explicit operator local_days() const noexcept;
```

**22** *Returns:* `local_days{sys_days{*this}.time_since_epoch()}`.

```cpp
constexpr bool ok() const noexcept;
```

**23** *Returns:* If `y_.ok()` is `true`, and `m_.ok()` is `true`, and `d_` is in the range `[1d, (y_/m_/last).day()]`, then returns `true`; otherwise returns `false`.
