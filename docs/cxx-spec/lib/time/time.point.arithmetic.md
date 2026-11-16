# 27.6.3 Arithmetic [time.point.arithmetic]

[Contents](../../README.md) · up: [27.6 Class template `time_point` \[time.point\]](time.point.md) · prev: [27.6.2 Observer \[time.point.observer\]](time.point.observer.md) · next: [27.6.4 Special values \[time.point.special\]](time.point.special.md)

```cpp
constexpr time_point& operator++();
```

**1** *Effects:* Equivalent to: `++d_`.

**2** *Returns:* `*this`.

```cpp
constexpr time_point operator++(int);
```

**3** *Effects:* Equivalent to: `return time_point{d_++};`

```cpp
constexpr time_point& operator--();
```

**4** *Effects:* Equivalent to: `--d_`.

**5** *Returns:* `*this`.

```cpp
constexpr time_point operator--(int);
```

**6** *Effects:* Equivalent to: `return time_point{d_--};`

```cpp
constexpr time_point& operator+=(const duration& d);
```

**7** *Effects:* Equivalent to: `d_ += d`.

**8** *Returns:* `*this`.

```cpp
constexpr time_point& operator-=(const duration& d);
```

**9** *Effects:* Equivalent to: `d_ -= d`.

**10** *Returns:* `*this`.
