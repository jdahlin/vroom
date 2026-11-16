# 27.8.3.2 Member functions [time.cal.day.members]

[Contents](../../README.md) · up: [27.8.3 Class `day` \[time.cal.day\]](time.cal.day.md) · prev: [27.8.3.1 Overview \[time.cal.day.overview\]](time.cal.day.overview.md) · next: [27.8.3.3 Non-member functions \[time.cal.day.nonmembers\]](time.cal.day.nonmembers.md)

```cpp
constexpr explicit day(unsigned d) noexcept;
```

**1** *Effects:* Initializes `d_` with `d`. The value held is unspecified if `d` is not in the range `[0, 255]`.

```cpp
constexpr day& operator++() noexcept;
```

**2** *Effects:* `++d_`.

**3** *Returns:* `*this`.

```cpp
constexpr day operator++(int) noexcept;
```

**4** *Effects:* `++(*this)`.

**5** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr day& operator--() noexcept;
```

**6** *Effects:* Equivalent to: `--d_`.

**7** *Returns:* `*this`.

```cpp
constexpr day operator--(int) noexcept;
```

**8** *Effects:* `--(*this)`.

**9** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr day& operator+=(const days& d) noexcept;
```

**10** *Effects:* `*this = *this + d`.

**11** *Returns:* `*this`.

```cpp
constexpr day& operator-=(const days& d) noexcept;
```

**12** *Effects:* `*this = *this - d`.

**13** *Returns:* `*this`.

```cpp
constexpr explicit operator unsigned() const noexcept;
```

**14** *Returns:* `d_`.

```cpp
constexpr bool ok() const noexcept;
```

**15** *Returns:* `1 <= d_ && d_ <= 31`.
