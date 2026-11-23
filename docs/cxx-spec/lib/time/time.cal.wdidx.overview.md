# 27.8.7.1 Overview [time.cal.wdidx.overview]

[Contents](../../README.md) · up: [27.8.7 Class `weekday_indexed` \[time.cal.wdidx\]](time.cal.wdidx.md) · prev: [27.8.7 Class `weekday_indexed` \[time.cal.wdidx\]](time.cal.wdidx.md) · next: [27.8.7.2 Member functions \[time.cal.wdidx.members\]](time.cal.wdidx.members.md)

```cpp
namespace std::chrono {
  class weekday_indexed {
    chrono::weekday  wd_;       // exposition only
    unsigned char    index_;    // exposition only

  public:
    weekday_indexed() = default;
    constexpr weekday_indexed(const chrono::weekday& wd, unsigned index) noexcept;

    constexpr chrono::weekday weekday() const noexcept;
    constexpr unsigned        index()   const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `weekday_indexed` represents a `weekday` and a small index in the range 1 to 5. This class is used to represent the first, second, third, fourth, or fifth weekday of a month.

**2** [*Note 1*: A `weekday_indexed` object can be constructed by indexing a `weekday` with an `unsigned`. — *end note*] [*Example 1*:

```cpp
constexpr auto wdi = Sunday[2]; // wdi is the second Sunday of an as yet unspecified month
static_assert(wdi.weekday() == Sunday);
static_assert(wdi.index() == 2);
```

— *end example*]

**3** `weekday_indexed` is a trivially copyable and standard-layout class type.
