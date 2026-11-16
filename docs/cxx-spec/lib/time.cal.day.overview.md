# 27.8.3.1 Overview [time.cal.day.overview]

[Contents](../README.md) · up: [27.8.3 Class `day` \[time.cal.day\]](time.cal.day.md) · prev: [27.8.3 Class `day` \[time.cal.day\]](time.cal.day.md) · next: [27.8.3.2 Member functions \[time.cal.day.members\]](time.cal.day.members.md)

```cpp
namespace std::chrono {
  class day {
    unsigned char d_;           // exposition only
  public:
    day() = default;
    constexpr explicit day(unsigned d) noexcept;

    constexpr day& operator++()    noexcept;
    constexpr day  operator++(int) noexcept;
    constexpr day& operator--()    noexcept;
    constexpr day  operator--(int) noexcept;

    constexpr day& operator+=(const days& d) noexcept;
    constexpr day& operator-=(const days& d) noexcept;

    constexpr explicit operator unsigned() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `day` represents a day of a month. It normally holds values in the range 1 to 31, but may hold non-negative values outside this range. It can be constructed with any `unsigned` value, which will be subsequently truncated to fit into `day`’s unspecified internal storage. `day` meets the *Cpp17EqualityComparable* ([Table 25](utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements, and participates in basic arithmetic with `days` objects, which represent a difference between two `day` objects.

**2** `day` is a trivially copyable and standard-layout class type.
