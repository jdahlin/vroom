# 27.8.6.1 Overview [time.cal.wd.overview]

[Contents](../README.md) · up: [27.8.6 Class `weekday` \[time.cal.wd\]](time.cal.wd.md) · prev: [27.8.6 Class `weekday` \[time.cal.wd\]](time.cal.wd.md) · next: [27.8.6.2 Member functions \[time.cal.wd.members\]](time.cal.wd.members.md)

```cpp
namespace std::chrono {
  class weekday {
    unsigned char wd_;          // exposition only
  public:
    weekday() = default;
    constexpr explicit weekday(unsigned wd) noexcept;
    constexpr weekday(const sys_days& dp) noexcept;
    constexpr explicit weekday(const local_days& dp) noexcept;

    constexpr weekday& operator++()    noexcept;
    constexpr weekday  operator++(int) noexcept;
    constexpr weekday& operator--()    noexcept;
    constexpr weekday  operator--(int) noexcept;

    constexpr weekday& operator+=(const days& d) noexcept;
    constexpr weekday& operator-=(const days& d) noexcept;

    constexpr unsigned c_encoding() const noexcept;
    constexpr unsigned iso_encoding() const noexcept;
    constexpr bool ok() const noexcept;

    constexpr weekday_indexed operator[](unsigned index) const noexcept;
    constexpr weekday_last    operator[](last_spec) const noexcept;
  };
}
```

**1** `weekday` represents a day of the week in the civil calendar. It normally holds values in the range `0` to `6`, corresponding to Sunday through Saturday, but it may hold non-negative values outside this range. It can be constructed with any `unsigned` value, which will be subsequently truncated to fit into `weekday`’s unspecified internal storage. `weekday` meets the *Cpp17EqualityComparable* ([Table 25](utility.arg.requirements.md#tab:cpp17.equalitycomparable)) requirements. [*Note 1*: `weekday` is not *Cpp17LessThanComparable* because there is no universal consensus on which day is the first day of the week. `weekday`’s arithmetic operations treat the days of the week as a circular range, with no beginning and no end. — *end note*]

**2** `weekday` is a trivially copyable and standard-layout class type.
