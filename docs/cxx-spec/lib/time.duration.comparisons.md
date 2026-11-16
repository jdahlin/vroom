# 27.5.6 Comparisons [time.duration.comparisons]

[Contents](../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.5 Non-member arithmetic \[time.duration.nonmember\]](time.duration.nonmember.md) · next: [27.5.7 Conversions \[time.duration.cast\]](time.duration.cast.md)

**1** In the function descriptions that follow, `CT` represents `common_type_t<A, B>`, where `A` and `B` are the types of the two arguments to the function.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr bool operator==(const duration<Rep1, Period1>& lhs,
                            const duration<Rep2, Period2>& rhs);
```

**2** *Returns:* `CT(lhs).count() == CT(rhs).count()`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr bool operator<(const duration<Rep1, Period1>& lhs,
                           const duration<Rep2, Period2>& rhs);
```

**3** *Returns:* `CT(lhs).count() < CT(rhs).count()`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr bool operator>(const duration<Rep1, Period1>& lhs,
                           const duration<Rep2, Period2>& rhs);
```

**4** *Returns:* `rhs < lhs`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr bool operator<=(const duration<Rep1, Period1>& lhs,
                            const duration<Rep2, Period2>& rhs);
```

**5** *Returns:* `!(rhs < lhs)`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr bool operator>=(const duration<Rep1, Period1>& lhs,
                            const duration<Rep2, Period2>& rhs);
```

**6** *Returns:* `!(lhs < rhs)`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
    requires three_way_comparable<typename CT::rep>
  constexpr auto operator<=>(const duration<Rep1, Period1>& lhs,
                             const duration<Rep2, Period2>& rhs);
```

**7** *Returns:* `CT(lhs).count() <=> CT(rhs).count()`.
