# 27.8.5.2 Member functions [time.cal.year.members]

[Contents](../README.md) · up: [27.8.5 Class `year` \[time.cal.year\]](time.cal.year.md) · prev: [27.8.5.1 Overview \[time.cal.year.overview\]](time.cal.year.overview.md) · next: [27.8.5.3 Non-member functions \[time.cal.year.nonmembers\]](time.cal.year.nonmembers.md)

```cpp
constexpr explicit year(int y) noexcept;
```

**1** *Effects:* Initializes `y_` with `y`. The value held is unspecified if `y` is not in the range `[-32767, 32767]`.

```cpp
constexpr year& operator++() noexcept;
```

**2** *Effects:* `++y_`.

**3** *Returns:* `*this`.

```cpp
constexpr year operator++(int) noexcept;
```

**4** *Effects:* `++(*this)`.

**5** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr year& operator--() noexcept;
```

**6** *Effects:* `--y_`.

**7** *Returns:* `*this`.

```cpp
constexpr year operator--(int) noexcept;
```

**8** *Effects:* `--(*this)`.

**9** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr year& operator+=(const years& y) noexcept;
```

**10** *Effects:* `*this = *this + y`.

**11** *Returns:* `*this`.

```cpp
constexpr year& operator-=(const years& y) noexcept;
```

**12** *Effects:* `*this = *this - y`.

**13** *Returns:* `*this`.

```cpp
constexpr year operator+() const noexcept;
```

**14** *Returns:* `*this`.

```cpp
constexpr year year::operator-() const noexcept;
```

**15** *Returns:* `year{-y_}`.

```cpp
constexpr bool is_leap() const noexcept;
```

**16** *Returns:* `y_ % 4 == 0 && (y_ % 100 != 0 || y_ % 400 == 0)`.

```cpp
constexpr explicit operator int() const noexcept;
```

**17** *Returns:* `y_`.

```cpp
constexpr bool ok() const noexcept;
```

**18** *Returns:* `min().y_ <= y_ && y_ <= max().y_`.

```cpp
static constexpr year min() noexcept;
```

**19** *Returns:* `year{-32767}`.

```cpp
static constexpr year max() noexcept;
```

**20** *Returns:* `year{32767}`.
