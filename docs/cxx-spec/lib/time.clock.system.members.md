# 27.7.1.2 Members [time.clock.system.members]

[Contents](../README.md) · up: [27.7.1 Class `system_clock` \[time.clock.system\]](time.clock.system.md) · prev: [27.7.1.1 Overview \[time.clock.system.overview\]](time.clock.system.overview.md) · next: [27.7.1.3 Non-member functions \[time.clock.system.nonmembers\]](time.clock.system.nonmembers.md)

```cpp
using system_clock::rep = unspecified;
```

**1** *Constraints:* `system_clock::duration::min() < system_clock::duration::zero()` is `true`. <br> [*Note 1*: This implies that `rep` is a signed type. — *end note*]

```cpp
static time_t to_time_t(const time_point& t) noexcept;
```

**2** *Returns:* A `time_t` object that represents the same point in time as `t` when both values are restricted to the coarser of the precisions of `time_t` and `time_point`. It is implementation-defined whether values are rounded or truncated to the required precision.

```cpp
static time_point from_time_t(time_t t) noexcept;
```

**3** *Returns:* A `time_point` object that represents the same point in time as `t` when both values are restricted to the coarser of the precisions of `time_t` and `time_point`. It is implementation-defined whether values are rounded or truncated to the required precision.
