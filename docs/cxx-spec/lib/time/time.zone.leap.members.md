# 27.11.8.2 Member functions [time.zone.leap.members]

[Contents](../../README.md) · up: [27.11.8 Class `leap_second` \[time.zone.leap\]](time.zone.leap.md) · prev: [27.11.8.1 Overview \[time.zone.leap.overview\]](time.zone.leap.overview.md) · next: [27.11.8.3 Non-member functions \[time.zone.leap.nonmembers\]](time.zone.leap.nonmembers.md)

```cpp
constexpr sys_seconds date() const noexcept;
```

**1** *Returns:* The date and time at which the leap second was inserted.

```cpp
constexpr seconds value() const noexcept;
```

**2** *Returns:* `+1s` to indicate a positive leap second or `-1s` to indicate a negative leap second. [*Note 1*: All leap seconds inserted up through 2019 were positive leap seconds. — *end note*]
