# 27.8.17.1 Overview [time.cal.ymwdlast.overview]

[Contents](../../README.md) · up: [27.8.17 Class `year_month_weekday_last` \[time.cal.ymwdlast\]](time.cal.ymwdlast.md) · prev: [27.8.17 Class `year_month_weekday_last` \[time.cal.ymwdlast\]](time.cal.ymwdlast.md) · next: [27.8.17.2 Member functions \[time.cal.ymwdlast.members\]](time.cal.ymwdlast.members.md)

```cpp
namespace std::chrono {
  class year_month_weekday_last {
    chrono::year         y_;    // exposition only
    chrono::month        m_;    // exposition only
    chrono::weekday_last wdl_;  // exposition only

  public:
    constexpr year_month_weekday_last(const chrono::year& y, const chrono::month& m,
                                      const chrono::weekday_last& wdl) noexcept;

    constexpr year_month_weekday_last& operator+=(const months& m) noexcept;
    constexpr year_month_weekday_last& operator-=(const months& m) noexcept;
    constexpr year_month_weekday_last& operator+=(const years& y)  noexcept;
    constexpr year_month_weekday_last& operator-=(const years& y)  noexcept;

    constexpr chrono::year         year()         const noexcept;
    constexpr chrono::month        month()        const noexcept;
    constexpr chrono::weekday      weekday()      const noexcept;
    constexpr chrono::weekday_last weekday_last() const noexcept;

    constexpr          operator sys_days()   const noexcept;
    constexpr explicit operator local_days() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `year_month_weekday_last` represents a specific year, month, and last weekday of the month. `year_month_weekday_last` is a field-based time point with a resolution of `days`, except that it is restricted to pointing to the last weekday of a year and month. [*Note 1*: `year_month_weekday_last` supports `years`- and `months`-oriented arithmetic, but not `days`-oriented arithmetic. For the latter, there is a conversion to `sys_days`, which efficiently supports `days`-oriented arithmetic. — *end note*] `year_month_weekday_last` meets the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) requirements.

**2** `year_month_weekday_last` is a trivially copyable and standard-layout class type.
