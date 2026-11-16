# 27.8.9.1 Overview [time.cal.md.overview]

[Contents](../../README.md) · up: [27.8.9 Class `month_day` \[time.cal.md\]](time.cal.md.md) · prev: [27.8.9 Class `month_day` \[time.cal.md\]](time.cal.md.md) · next: [27.8.9.2 Member functions \[time.cal.md.members\]](time.cal.md.members.md)

```cpp
namespace std::chrono {
  class month_day {
    chrono::month m_;           // exposition only
    chrono::day   d_;           // exposition only

  public:
    month_day() = default;
    constexpr month_day(const chrono::month& m, const chrono::day& d) noexcept;

    constexpr chrono::month month() const noexcept;
    constexpr chrono::day   day()   const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `month_day` represents a specific day of a specific month, but with an unspecified year. `month_day` meets the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](../library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements.

**2** `month_day` is a trivially copyable and standard-layout class type.
