# 27.8.16.1 Overview [time.cal.ymwd.overview]

[Contents](../README.md) · up: [27.8.16 Class `year_month_weekday` \[time.cal.ymwd\]](time.cal.ymwd.md) · prev: [27.8.16 Class `year_month_weekday` \[time.cal.ymwd\]](time.cal.ymwd.md) · next: [27.8.16.2 Member functions \[time.cal.ymwd.members\]](time.cal.ymwd.members.md)

```cpp
namespace std::chrono {
  class year_month_weekday {
    chrono::year            y_;         // exposition only
    chrono::month           m_;         // exposition only
    chrono::weekday_indexed wdi_;       // exposition only

  public:
    year_month_weekday() = default;
    constexpr year_month_weekday(const chrono::year& y, const chrono::month& m,
                                 const chrono::weekday_indexed& wdi) noexcept;
    constexpr year_month_weekday(const sys_days& dp) noexcept;
    constexpr explicit year_month_weekday(const local_days& dp) noexcept;

    constexpr year_month_weekday& operator+=(const months& m) noexcept;
    constexpr year_month_weekday& operator-=(const months& m) noexcept;
    constexpr year_month_weekday& operator+=(const years& y)  noexcept;
    constexpr year_month_weekday& operator-=(const years& y)  noexcept;

    constexpr chrono::year            year()            const noexcept;
    constexpr chrono::month           month()           const noexcept;
    constexpr chrono::weekday         weekday()         const noexcept;
    constexpr unsigned                index()           const noexcept;
    constexpr chrono::weekday_indexed weekday_indexed() const noexcept;

    constexpr          operator sys_days()   const noexcept;
    constexpr explicit operator local_days() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `year_month_weekday` represents a specific year, month, and *n*<sup></sup>*th* weekday of the month. `year_month_weekday` is a field-based time point with a resolution of `days`. [*Note 1*: `year_month_weekday` supports `years`- and `months`-oriented arithmetic, but not `days`-oriented arithmetic. For the latter, there is a conversion to `sys_days`, which efficiently supports `days`-oriented arithmetic. — *end note*] `year_month_weekday` meets the *Cpp17EqualityComparable* ([Table 25](utility.arg.requirements.md#tab:cpp17.equalitycomparable)) requirements.

**2** `year_month_weekday` is a trivially copyable and standard-layout class type.
