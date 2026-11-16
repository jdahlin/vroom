# 27.8.4.1 Overview [time.cal.month.overview]

[Contents](../../README.md) · up: [27.8.4 Class `month` \[time.cal.month\]](time.cal.month.md) · prev: [27.8.4 Class `month` \[time.cal.month\]](time.cal.month.md) · next: [27.8.4.2 Member functions \[time.cal.month.members\]](time.cal.month.members.md)

```cpp
namespace std::chrono {
  class month {
    unsigned char m_;           // exposition only
  public:
    month() = default;
    constexpr explicit month(unsigned m) noexcept;

    constexpr month& operator++()    noexcept;
    constexpr month  operator++(int) noexcept;
    constexpr month& operator--()    noexcept;
    constexpr month  operator--(int) noexcept;

    constexpr month& operator+=(const months& m) noexcept;
    constexpr month& operator-=(const months& m) noexcept;

    constexpr explicit operator unsigned() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `month` represents a month of a year. It normally holds values in the range 1 to 12, but may hold non-negative values outside this range. It can be constructed with any `unsigned` value, which will be subsequently truncated to fit into `month`’s unspecified internal storage. `month` meets the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](../library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements, and participates in basic arithmetic with `months` objects, which represent a difference between two `month` objects.

**2** `month` is a trivially copyable and standard-layout class type.
