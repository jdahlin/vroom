# 27.6.1 Constructors [time.point.cons]

[Contents](../README.md) · up: [27.6 Class template `time_point` \[time.point\]](time.point.md) · prev: [27.6 Class template `time_point` \[time.point\]](time.point.md) · next: [27.6.2 Observer \[time.point.observer\]](time.point.observer.md)

```cpp
constexpr time_point();
```

**1** *Effects:* Initializes `d_` with `duration::zero()`. Such a `time_point` object represents the epoch.

```cpp
constexpr explicit time_point(const duration& d);
```

**2** *Effects:* Initializes `d_` with `d`. Such a `time_point` object represents the epoch `+ d`.

```cpp
template<class Duration2>
  constexpr time_point(const time_point<clock, Duration2>& t);
```

**3** *Constraints:* `is_convertible_v<Duration2, duration>` is `true`.

**4** *Effects:* Initializes `d_` with `t.time_since_epoch()`.
