# 27.6.6 Comparisons [time.point.comparisons]

[Contents](../../README.md) · up: [27.6 Class template `time_point` \[time.point\]](time.point.md) · prev: [27.6.5 Non-member arithmetic \[time.point.nonmember\]](time.point.nonmember.md) · next: [27.6.7 Conversions \[time.point.cast\]](time.point.cast.md)

```cpp
template<class Clock, class Duration1, class Duration2>
  constexpr bool operator==(const time_point<Clock, Duration1>& lhs,
                            const time_point<Clock, Duration2>& rhs);
```

**1** *Returns:* `lhs.time_since_epoch() == rhs.time_since_epoch()`.

```cpp
template<class Clock, class Duration1, class Duration2>
  constexpr bool operator<(const time_point<Clock, Duration1>& lhs,
                           const time_point<Clock, Duration2>& rhs);
```

**2** *Returns:* `lhs.time_since_epoch() < rhs.time_since_epoch()`.

```cpp
template<class Clock, class Duration1, class Duration2>
  constexpr bool operator>(const time_point<Clock, Duration1>& lhs,
                           const time_point<Clock, Duration2>& rhs);
```

**3** *Returns:* `rhs < lhs`.

```cpp
template<class Clock, class Duration1, class Duration2>
  constexpr bool operator<=(const time_point<Clock, Duration1>& lhs,
                            const time_point<Clock, Duration2>& rhs);
```

**4** *Returns:* `!(rhs < lhs)`.

```cpp
template<class Clock, class Duration1, class Duration2>
  constexpr bool operator>=(const time_point<Clock, Duration1>& lhs,
                            const time_point<Clock, Duration2>& rhs);
```

**5** *Returns:* `!(lhs < rhs)`.

```cpp
template<class Clock, class Duration1,
         three_way_comparable_with<Duration1> Duration2>
  constexpr auto operator<=>(const time_point<Clock, Duration1>& lhs,
                             const time_point<Clock, Duration2>& rhs);
```

**6** *Returns:* `lhs.time_since_epoch() <=> rhs.time_since_epoch()`.
