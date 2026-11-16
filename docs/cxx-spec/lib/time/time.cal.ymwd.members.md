# 27.8.16.2 Member functions [time.cal.ymwd.members]

[Contents](../../README.md) · up: [27.8.16 Class `year_month_weekday` \[time.cal.ymwd\]](time.cal.ymwd.md) · prev: [27.8.16.1 Overview \[time.cal.ymwd.overview\]](time.cal.ymwd.overview.md) · next: [27.8.16.3 Non-member functions \[time.cal.ymwd.nonmembers\]](time.cal.ymwd.nonmembers.md)

```cpp
constexpr year_month_weekday(const chrono::year& y, const chrono::month& m,
                             const chrono::weekday_indexed& wdi) noexcept;
```

**1** *Effects:* Initializes `y_` with `y`, `m_` with `m`, and `wdi_` with `wdi`.

```cpp
constexpr year_month_weekday(const sys_days& dp) noexcept;
```

**2** *Effects:* Constructs an object of type `year_month_weekday` which corresponds to the date represented by `dp`.

**3** *Remarks:* For any value `ymdl` of type `year_month_weekday` for which `ymdl.ok()` is `true`, `ymdl == year_month_weekday{sys_days{ymdl}}` is `true`.

```cpp
constexpr explicit year_month_weekday(const local_days& dp) noexcept;
```

**4** *Effects:* Equivalent to constructing with `sys_days{dp.time_since_epoch()}`.

```cpp
constexpr year_month_weekday& operator+=(const months& m) noexcept;
```

**5** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**6** *Effects:* `*this = *this + m`.

**7** *Returns:* `*this`.

```cpp
constexpr year_month_weekday& operator-=(const months& m) noexcept;
```

**8** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**9** *Effects:* `*this = *this - m`.

**10** *Returns:* `*this`.

```cpp
constexpr year_month_weekday& operator+=(const years& y) noexcept;
```

**11** *Effects:* `*this = *this + y`.

**12** *Returns:* `*this`.

```cpp
constexpr year_month_weekday& operator-=(const years& y) noexcept;
```

**13** *Effects:* `*this = *this - y`.

**14** *Returns:* `*this`.

```cpp
constexpr chrono::year year() const noexcept;
```

**15** *Returns:* `y_`.

```cpp
constexpr chrono::month month() const noexcept;
```

**16** *Returns:* `m_`.

```cpp
constexpr chrono::weekday weekday() const noexcept;
```

**17** *Returns:* `wdi_.weekday()`.

```cpp
constexpr unsigned index() const noexcept;
```

**18** *Returns:* `wdi_.index()`.

```cpp
constexpr chrono::weekday_indexed weekday_indexed() const noexcept;
```

**19** *Returns:* `wdi_`.

```cpp
constexpr operator sys_days() const noexcept;
```

**20** *Returns:* If `y_.ok() && m_.ok() && wdi_.weekday().ok()`, returns a `sys_days` that represents the date `(index() - 1) * 7` days after the first `weekday()` of `year()/month()`. If `index()` is 0 the returned `sys_days` represents the date 7 days prior to the first `weekday()` of `year()/month()`. Otherwise the returned value is unspecified.

```cpp
constexpr explicit operator local_days() const noexcept;
```

**21** *Returns:* `local_days{sys_days{*this}.time_since_epoch()}`.

```cpp
constexpr bool ok() const noexcept;
```

**22** *Returns:* If any of `y_.ok()`, `m_.ok()`, or `wdi_.ok()` is `false`, returns `false`. Otherwise, if `*this` represents a valid date, returns `true`. Otherwise, returns `false`.
