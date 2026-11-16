# 27.8.15.1 Overview [time.cal.ymdlast.overview]

[Contents](../../README.md) · up: [27.8.15 Class `year_month_day_last` \[time.cal.ymdlast\]](time.cal.ymdlast.md) · prev: [27.8.15 Class `year_month_day_last` \[time.cal.ymdlast\]](time.cal.ymdlast.md) · next: [27.8.15.2 Member functions \[time.cal.ymdlast.members\]](time.cal.ymdlast.members.md)

```cpp
namespace std::chrono {
  class year_month_day_last {
    chrono::year           y_;          // exposition only
    chrono::month_day_last mdl_;        // exposition only

  public:
    constexpr year_month_day_last(const chrono::year& y,
                                  const chrono::month_day_last& mdl) noexcept;

    constexpr year_month_day_last& operator+=(const months& m) noexcept;
    constexpr year_month_day_last& operator-=(const months& m) noexcept;
    constexpr year_month_day_last& operator+=(const years& y)  noexcept;
    constexpr year_month_day_last& operator-=(const years& y)  noexcept;

    constexpr chrono::year           year()           const noexcept;
    constexpr chrono::month          month()          const noexcept;
    constexpr chrono::month_day_last month_day_last() const noexcept;
    constexpr chrono::day            day()            const noexcept;

    constexpr          operator sys_days()   const noexcept;
    constexpr explicit operator local_days() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `year_month_day_last` represents the last day of a specific year and month. `year_month_day_last` is a field-based time point with a resolution of `days`, except that it is restricted to pointing to the last day of a year and month. [*Note 1*: `year_month_day_last` supports `years`- and `months`-oriented arithmetic, but not `days`-oriented arithmetic. For the latter, there is a conversion to `sys_days`, which efficiently supports `days`-oriented arithmetic. — *end note*] `year_month_day_last` meets the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](../library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements.

**2** `year_month_day_last` is a trivially copyable and standard-layout class type.
