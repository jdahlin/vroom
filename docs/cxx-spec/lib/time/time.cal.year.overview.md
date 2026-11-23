# 27.8.5.1 Overview [time.cal.year.overview]

[Contents](../../README.md) · up: [27.8.5 Class `year` \[time.cal.year\]](time.cal.year.md) · prev: [27.8.5 Class `year` \[time.cal.year\]](time.cal.year.md) · next: [27.8.5.2 Member functions \[time.cal.year.members\]](time.cal.year.members.md)

```cpp
namespace std::chrono {
  class year {
    short y_;                   // exposition only
  public:
    year() = default;
    constexpr explicit year(int y) noexcept;

    constexpr year& operator++()    noexcept;
    constexpr year  operator++(int) noexcept;
    constexpr year& operator--()    noexcept;
    constexpr year  operator--(int) noexcept;

    constexpr year& operator+=(const years& y) noexcept;
    constexpr year& operator-=(const years& y) noexcept;

    constexpr year operator+() const noexcept;
    constexpr year operator-() const noexcept;

    constexpr bool is_leap() const noexcept;

    constexpr explicit operator int() const noexcept;
    constexpr bool ok() const noexcept;

    static constexpr year min() noexcept;
    static constexpr year max() noexcept;
  };
}
```

**1** `year` represents a year in the civil calendar. It can represent values in the range `[min(), max()]`. It can be constructed with any `int` value, which will be subsequently truncated to fit into `year`’s unspecified internal storage. `year` meets the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](../library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements, and participates in basic arithmetic with `years` objects, which represent a difference between two `year` objects.

**2** `year` is a trivially copyable and standard-layout class type.
