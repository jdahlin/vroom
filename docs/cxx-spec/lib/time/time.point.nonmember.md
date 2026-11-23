# 27.6.5 Non-member arithmetic [time.point.nonmember]

[Contents](../../README.md) · up: [27.6 Class template `time_point` \[time.point\]](time.point.md) · prev: [27.6.4 Special values \[time.point.special\]](time.point.special.md) · next: [27.6.6 Comparisons \[time.point.comparisons\]](time.point.comparisons.md)

```cpp
template<class Clock, class Duration1, class Rep2, class Period2>
  constexpr time_point<Clock, common_type_t<Duration1, duration<Rep2, Period2>>>
    operator+(const time_point<Clock, Duration1>& lhs, const duration<Rep2, Period2>& rhs);
```

**1** *Returns:* `CT(lhs.time_since_epoch() + rhs)`, where `CT` is the type of the return value.

```cpp
template<class Rep1, class Period1, class Clock, class Duration2>
  constexpr time_point<Clock, common_type_t<duration<Rep1, Period1>, Duration2>>
    operator+(const duration<Rep1, Period1>& lhs, const time_point<Clock, Duration2>& rhs);
```

**2** *Returns:* `rhs + lhs`.

```cpp
template<class Clock, class Duration1, class Rep2, class Period2>
  constexpr time_point<Clock, common_type_t<Duration1, duration<Rep2, Period2>>>
    operator-(const time_point<Clock, Duration1>& lhs, const duration<Rep2, Period2>& rhs);
```

**3** *Returns:* `CT(lhs.time_since_epoch() - rhs)`, where `CT` is the type of the return value.

```cpp
template<class Clock, class Duration1, class Duration2>
  constexpr common_type_t<Duration1, Duration2>
    operator-(const time_point<Clock, Duration1>& lhs, const time_point<Clock, Duration2>& rhs);
```

**4** *Returns:* `lhs.time_since_epoch() - rhs.time_since_epoch()`.
