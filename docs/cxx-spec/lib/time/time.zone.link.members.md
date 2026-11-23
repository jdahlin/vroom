# 27.11.9.2 Member functions [time.zone.link.members]

[Contents](../../README.md) · up: [27.11.9 Class `time_zone_link` \[time.zone.link\]](time.zone.link.md) · prev: [27.11.9.1 Overview \[time.zone.link.overview\]](time.zone.link.overview.md) · next: [27.11.9.3 Non-member functions \[time.zone.link.nonmembers\]](time.zone.link.nonmembers.md)

```cpp
string_view name() const noexcept;
```

**1** *Returns:* The alternative name for the time zone.

```cpp
string_view target() const noexcept;
```

**2** *Returns:* The name of the `time_zone` for which this `time_zone_link` provides an alternative name.
