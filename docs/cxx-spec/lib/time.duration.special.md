# 27.5.4 Special values [time.duration.special]

[Contents](../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.3 Arithmetic \[time.duration.arithmetic\]](time.duration.arithmetic.md) · next: [27.5.5 Non-member arithmetic \[time.duration.nonmember\]](time.duration.nonmember.md)

```cpp
static constexpr duration zero() noexcept;
```

**1** *Returns:* `duration(duration_values<rep>::zero())`.

```cpp
static constexpr duration min() noexcept;
```

**2** *Returns:* `duration(duration_values<rep>::min())`.

```cpp
static constexpr duration max() noexcept;
```

**3** *Returns:* `duration(duration_values<rep>::max())`.
