# 27.11.9.3 Non-member functions [time.zone.link.nonmembers]

[Contents](../../README.md) · up: [27.11.9 Class `time_zone_link` \[time.zone.link\]](time.zone.link.md) · prev: [27.11.9.2 Member functions \[time.zone.link.members\]](time.zone.link.members.md) · next: [27.12 Formatting \[time.format\]](time.format.md)

```cpp
bool operator==(const time_zone_link& x, const time_zone_link& y) noexcept;
```

**1** *Returns:* `x.name() == y.name()`.

```cpp
strong_ordering operator<=>(const time_zone_link& x, const time_zone_link& y) noexcept;
```

**2** *Returns:* `x.name() <=> y.name()`.
