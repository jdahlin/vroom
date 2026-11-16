# 27.8.13.1 Overview [time.cal.ym.overview]

[Contents](../../README.md) · up: [27.8.13 Class `year_month` \[time.cal.ym\]](time.cal.ym.md) · prev: [27.8.13 Class `year_month` \[time.cal.ym\]](time.cal.ym.md) · next: [27.8.13.2 Member functions \[time.cal.ym.members\]](time.cal.ym.members.md)

```cpp
namespace std::chrono {
  class year_month {
    chrono::year  y_;           // exposition only
    chrono::month m_;           // exposition only

  public:
    year_month() = default;
    constexpr year_month(const chrono::year& y, const chrono::month& m) noexcept;

    constexpr chrono::year  year()  const noexcept;
    constexpr chrono::month month() const noexcept;

    constexpr year_month& operator+=(const months& dm) noexcept;
    constexpr year_month& operator-=(const months& dm) noexcept;
    constexpr year_month& operator+=(const years& dy)  noexcept;
    constexpr year_month& operator-=(const years& dy)  noexcept;

    constexpr bool ok() const noexcept;
  };
}
```

**1** `year_month` represents a specific month of a specific year, but with an unspecified day. `year_month` is a field-based time point with a resolution of `months`. `year_month` meets the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](../library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements.

**2** `year_month` is a trivially copyable and standard-layout class type.
