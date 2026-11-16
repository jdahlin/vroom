# 27.11.8.3 Non-member functions [time.zone.leap.nonmembers]

[Contents](../../README.md) · up: [27.11.8 Class `leap_second` \[time.zone.leap\]](time.zone.leap.md) · prev: [27.11.8.2 Member functions \[time.zone.leap.members\]](time.zone.leap.members.md) · next: [27.11.9 Class `time_zone_link` \[time.zone.link\]](time.zone.link.md)

```cpp
constexpr bool operator==(const leap_second& x, const leap_second& y) noexcept;
```

**1** *Returns:* `x.date() == y.date()`.

```cpp
constexpr strong_ordering operator<=>(const leap_second& x, const leap_second& y) noexcept;
```

**2** *Returns:* `x.date() <=> y.date()`.

```cpp
template<class Duration>
  constexpr bool operator==(const leap_second& x, const sys_time<Duration>& y) noexcept;
```

**3** *Returns:* `x.date() == y`.

```cpp
template<class Duration>
  constexpr bool operator<(const leap_second& x, const sys_time<Duration>& y) noexcept;
```

**4** *Returns:* `x.date() < y`.

```cpp
template<class Duration>
  constexpr bool operator<(const sys_time<Duration>& x, const leap_second& y) noexcept;
```

**5** *Returns:* `x < y.date()`.

```cpp
template<class Duration>
  constexpr bool operator>(const leap_second& x, const sys_time<Duration>& y) noexcept;
```

**6** *Returns:* `y < x`.

```cpp
template<class Duration>
  constexpr bool operator>(const sys_time<Duration>& x, const leap_second& y) noexcept;
```

**7** *Returns:* `y < x`.

```cpp
template<class Duration>
  constexpr bool operator<=(const leap_second& x, const sys_time<Duration>& y) noexcept;
```

**8** *Returns:* `!(y < x)`.

```cpp
template<class Duration>
  constexpr bool operator<=(const sys_time<Duration>& x, const leap_second& y) noexcept;
```

**9** *Returns:* `!(y < x)`.

```cpp
template<class Duration>
  constexpr bool operator>=(const leap_second& x, const sys_time<Duration>& y) noexcept;
```

**10** *Returns:* `!(x < y)`.

```cpp
template<class Duration>
  constexpr bool operator>=(const sys_time<Duration>& x, const leap_second& y) noexcept;
```

**11** *Returns:* `!(x < y)`.

```cpp
template<class Duration>
  requires three_way_comparable_with<sys_seconds, sys_time<Duration>>
  constexpr auto operator<=>(const leap_second& x, const sys_time<Duration>& y) noexcept;
```

**12** *Returns:* `x.date() <=> y`.
