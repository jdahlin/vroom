# 27.8.17.2 Member functions [time.cal.ymwdlast.members]

[Contents](../README.md) · up: [27.8.17 Class `year_month_weekday_last` \[time.cal.ymwdlast\]](time.cal.ymwdlast.md) · prev: [27.8.17.1 Overview \[time.cal.ymwdlast.overview\]](time.cal.ymwdlast.overview.md) · next: [27.8.17.3 Non-member functions \[time.cal.ymwdlast.nonmembers\]](time.cal.ymwdlast.nonmembers.md)

```cpp
constexpr year_month_weekday_last(const chrono::year& y, const chrono::month& m,
                                  const chrono::weekday_last& wdl) noexcept;
```

**1** *Effects:* Initializes `y_` with `y`, `m_` with `m`, and `wdl_` with `wdl`.

```cpp
constexpr year_month_weekday_last& operator+=(const months& m) noexcept;
```

**2** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**3** *Effects:* `*this = *this + m`.

**4** *Returns:* `*this`.

```cpp
constexpr year_month_weekday_last& operator-=(const months& m) noexcept;
```

**5** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../over.ics.rank.md)).

**6** *Effects:* `*this = *this - m`.

**7** *Returns:* `*this`.

```cpp
constexpr year_month_weekday_last& operator+=(const years& y) noexcept;
```

**8** *Effects:* `*this = *this + y`.

**9** *Returns:* `*this`.

```cpp
constexpr year_month_weekday_last& operator-=(const years& y) noexcept;
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

**13** *Returns:* `m_`.

```cpp
constexpr chrono::weekday weekday() const noexcept;
```

**14** *Returns:* `wdl_.weekday()`.

```cpp
constexpr chrono::weekday_last weekday_last() const noexcept;
```

**15** *Returns:* `wdl_`.

```cpp
constexpr operator sys_days() const noexcept;
```

**16** *Returns:* If `ok() == true`, returns a `sys_days` that represents the last `weekday()` of `year()/month()`. Otherwise the returned value is unspecified.

```cpp
constexpr explicit operator local_days() const noexcept;
```

**17** *Returns:* `local_days{sys_days{*this}.time_since_epoch()}`.

```cpp
constexpr bool ok() const noexcept;
```

**18** *Returns:* `y_.ok() && m_.ok() && wdl_.ok()`.
