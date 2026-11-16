# 27.8.11.1 Overview [time.cal.mwd.overview]

[Contents](../README.md) · up: [27.8.11 Class `month_weekday` \[time.cal.mwd\]](time.cal.mwd.md) · prev: [27.8.11 Class `month_weekday` \[time.cal.mwd\]](time.cal.mwd.md) · next: [27.8.11.2 Member functions \[time.cal.mwd.members\]](time.cal.mwd.members.md)

```cpp
namespace std::chrono {
  class month_weekday {
    chrono::month           m_;         // exposition only
    chrono::weekday_indexed wdi_;       // exposition only
  public:
    constexpr month_weekday(const chrono::month& m, const chrono::weekday_indexed& wdi) noexcept;

    constexpr chrono::month           month()           const noexcept;
    constexpr chrono::weekday_indexed weekday_indexed() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `month_weekday` represents the *n*<sup></sup>*th* weekday of a month, of an as yet unspecified year. To do this the `month_weekday` stores a `month` and a `weekday_indexed`.

**2** [*Example 1*:

```cpp
constexpr auto mwd
    = February/Tuesday[3];              // mwd is the third Tuesday of February of an as yet unspecified year
static_assert(mwd.month() == February);
static_assert(mwd.weekday_indexed() == Tuesday[3]);
```

— *end example*]

**3** `month_weekday` is a trivially copyable and standard-layout class type.
