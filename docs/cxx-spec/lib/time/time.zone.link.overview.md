# 27.11.9.1 Overview [time.zone.link.overview]

[Contents](../../README.md) · up: [27.11.9 Class `time_zone_link` \[time.zone.link\]](time.zone.link.md) · prev: [27.11.9 Class `time_zone_link` \[time.zone.link\]](time.zone.link.md) · next: [27.11.9.2 Member functions \[time.zone.link.members\]](time.zone.link.members.md)

```cpp
namespace std::chrono {
  class time_zone_link {
  public:
    time_zone_link(time_zone_link&&)            = default;
    time_zone_link& operator=(time_zone_link&&) = default;

    // unspecified additional constructors

    string_view name()   const noexcept;
    string_view target() const noexcept;
  };
}
```

**1** A `time_zone_link` specifies an alternative name for a `time_zone`. `time_zone_link`s are constructed when the time zone database is initialized.
