# 27.11.5.3 Non-member functions [time.zone.nonmembers]

[Contents](../README.md) · up: [27.11.5 Class `time_zone` \[time.zone.timezone\]](time.zone.timezone.md) · prev: [27.11.5.2 Member functions \[time.zone.members\]](time.zone.members.md) · next: [27.11.6 Class template `zoned_traits` \[time.zone.zonedtraits\]](time.zone.zonedtraits.md)

```cpp
bool operator==(const time_zone& x, const time_zone& y) noexcept;
```

**1** *Returns:* `x.name() == y.name()`.

```cpp
strong_ordering operator<=>(const time_zone& x, const time_zone& y) noexcept;
```

**2** *Returns:* `x.name() <=> y.name()`.
