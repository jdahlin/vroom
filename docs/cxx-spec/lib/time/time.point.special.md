# 27.6.4 Special values [time.point.special]

[Contents](../../README.md) · up: [27.6 Class template `time_point` \[time.point\]](time.point.md) · prev: [27.6.3 Arithmetic \[time.point.arithmetic\]](time.point.arithmetic.md) · next: [27.6.5 Non-member arithmetic \[time.point.nonmember\]](time.point.nonmember.md)

```cpp
static constexpr time_point min() noexcept;
```

**1** *Returns:* `time_point(duration::min())`.

```cpp
static constexpr time_point max() noexcept;
```

**2** *Returns:* `time_point(duration::max())`.
