# 27.5.9 Algorithms [time.duration.alg]

[Contents](../../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.8 Suffixes for duration literals \[time.duration.literals\]](time.duration.literals.md) · next: [27.5.10 I/O \[time.duration.io\]](time.duration.io.md)

```cpp
template<class Rep, class Period>
  constexpr duration<Rep, Period> abs(duration<Rep, Period> d);
```

**1** *Constraints:* `numeric_limits<Rep>::is_signed` is `true`.

**2** *Returns:* If `d >= d.zero()`, return `d`, otherwise return `-d`.
