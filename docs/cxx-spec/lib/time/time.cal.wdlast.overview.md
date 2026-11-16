# 27.8.8.1 Overview [time.cal.wdlast.overview]

[Contents](../../README.md) · up: [27.8.8 Class `weekday_last` \[time.cal.wdlast\]](time.cal.wdlast.md) · prev: [27.8.8 Class `weekday_last` \[time.cal.wdlast\]](time.cal.wdlast.md) · next: [27.8.8.2 Member functions \[time.cal.wdlast.members\]](time.cal.wdlast.members.md)

```cpp
namespace std::chrono {
  class weekday_last {
    chrono::weekday wd_;                // exposition only

    public:
    constexpr explicit weekday_last(const chrono::weekday& wd) noexcept;

    constexpr chrono::weekday weekday() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `weekday_last` represents the last weekday of a month.

**2** [*Note 1*: A `weekday_last` object can be constructed by indexing a `weekday` with `last`. — *end note*] [*Example 1*:

```cpp
constexpr auto wdl = Sunday[last];      // wdl is the last Sunday of an as yet unspecified month
static_assert(wdl.weekday() == Sunday);
```

— *end example*]

**3** `weekday_last` is a trivially copyable and standard-layout class type.
