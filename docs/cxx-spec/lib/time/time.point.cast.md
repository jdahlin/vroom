# 27.6.7 Conversions [time.point.cast]

[Contents](../../README.md) · up: [27.6 Class template `time_point` \[time.point\]](time.point.md) · prev: [27.6.6 Comparisons \[time.point.comparisons\]](time.point.comparisons.md) · next: [27.7 Clocks \[time.clock\]](time.clock.md)

```cpp
template<class ToDuration, class Clock, class Duration>
  constexpr time_point<Clock, ToDuration> time_point_cast(const time_point<Clock, Duration>& t);
```

**1** *Constraints:* `ToDuration` is a specialization of `duration`.

**2** *Returns:*

```cpp
time_point<Clock, ToDuration>(duration_cast<ToDuration>(t.time_since_epoch()))
```

```cpp
template<class ToDuration, class Clock, class Duration>
  constexpr time_point<Clock, ToDuration> floor(const time_point<Clock, Duration>& tp);
```

**3** *Constraints:* `ToDuration` is a specialization of `duration`.

**4** *Returns:* `time_point<Clock, ToDuration>(floor<ToDuration>(tp.time_since_epoch()))`.

```cpp
template<class ToDuration, class Clock, class Duration>
  constexpr time_point<Clock, ToDuration> ceil(const time_point<Clock, Duration>& tp);
```

**5** *Constraints:* `ToDuration` is a specialization of `duration`.

**6** *Returns:* `time_point<Clock, ToDuration>(ceil<ToDuration>(tp.time_since_epoch()))`.

```cpp
template<class ToDuration, class Clock, class Duration>
  constexpr time_point<Clock, ToDuration> round(const time_point<Clock, Duration>& tp);
```

**7** *Constraints:* `ToDuration` is a specialization of `duration`, and `treat_as_floating_point_v<typename ToDuration::rep>` is `false`.

**8** *Returns:* `time_point<Clock, ToDuration>(round<ToDuration>(tp.time_since_epoch()))`.
