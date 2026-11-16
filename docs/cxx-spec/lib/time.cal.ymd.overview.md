# 27.8.14.1 Overview [time.cal.ymd.overview]

[Contents](../README.md) · up: [27.8.14 Class `year_month_day` \[time.cal.ymd\]](time.cal.ymd.md) · prev: [27.8.14 Class `year_month_day` \[time.cal.ymd\]](time.cal.ymd.md) · next: [27.8.14.2 Member functions \[time.cal.ymd.members\]](time.cal.ymd.members.md)

```cpp
namespace std::chrono {
  class year_month_day {
    chrono::year  y_;           // exposition only
    chrono::month m_;           // exposition only
    chrono::day   d_;           // exposition only

  public:
    year_month_day() = default;
    constexpr year_month_day(const chrono::year& y, const chrono::month& m,
                             const chrono::day& d) noexcept;
    constexpr year_month_day(const year_month_day_last& ymdl) noexcept;
    constexpr year_month_day(const sys_days& dp) noexcept;
    constexpr explicit year_month_day(const local_days& dp) noexcept;

    constexpr year_month_day& operator+=(const months& m) noexcept;
    constexpr year_month_day& operator-=(const months& m) noexcept;
    constexpr year_month_day& operator+=(const years& y)  noexcept;
    constexpr year_month_day& operator-=(const years& y)  noexcept;

    constexpr chrono::year  year()  const noexcept;
    constexpr chrono::month month() const noexcept;
    constexpr chrono::day   day()   const noexcept;

    constexpr          operator sys_days()   const noexcept;
    constexpr explicit operator local_days() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `year_month_day` represents a specific year, month, and day. `year_month_day` is a field-based time point with a resolution of `days`. [*Note 1*: `year_month_day` supports `years`- and `months`-oriented arithmetic, but not `days`-oriented arithmetic. For the latter, there is a conversion to `sys_days`, which efficiently supports `days`-oriented arithmetic. — *end note*] `year_month_day` meets the *Cpp17EqualityComparable* ([Table 25](utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements.

**2** `year_month_day` is a trivially copyable and standard-layout class type.
