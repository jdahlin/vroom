# 27.8.12.1 Overview [time.cal.mwdlast.overview]

[Contents](../README.md) · up: [27.8.12 Class `month_weekday_last` \[time.cal.mwdlast\]](time.cal.mwdlast.md) · prev: [27.8.12 Class `month_weekday_last` \[time.cal.mwdlast\]](time.cal.mwdlast.md) · next: [27.8.12.2 Member functions \[time.cal.mwdlast.members\]](time.cal.mwdlast.members.md)

```cpp
namespace std::chrono {
  class month_weekday_last {
    chrono::month        m_;    // exposition only
    chrono::weekday_last wdl_;  // exposition only
  public:
    constexpr month_weekday_last(const chrono::month& m,
                                 const chrono::weekday_last& wdl) noexcept;

    constexpr chrono::month        month()        const noexcept;
    constexpr chrono::weekday_last weekday_last() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `month_weekday_last` represents the last weekday of a month, of an as yet unspecified year. To do this the `month_weekday_last` stores a `month` and a `weekday_last`.

**2** [*Example 1*:

```cpp
constexpr auto mwd
    = February/Tuesday[last];   // mwd is the last Tuesday of February of an as yet unspecified year
static_assert(mwd.month() == February);
static_assert(mwd.weekday_last() == Tuesday[last]);
```

— *end example*]

**3** `month_weekday_last` is a trivially copyable and standard-layout class type.
