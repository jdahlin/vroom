# 27.8.4.2 Member functions [time.cal.month.members]

[Contents](../README.md) · up: [27.8.4 Class `month` \[time.cal.month\]](time.cal.month.md) · prev: [27.8.4.1 Overview \[time.cal.month.overview\]](time.cal.month.overview.md) · next: [27.8.4.3 Non-member functions \[time.cal.month.nonmembers\]](time.cal.month.nonmembers.md)

```cpp
constexpr explicit month(unsigned m) noexcept;
```

**1** *Effects:* Initializes `m_` with `m`. The value held is unspecified if `m` is not in the range `[0, 255]`.

```cpp
constexpr month& operator++() noexcept;
```

**2** *Effects:* `*this += months{1}`.

**3** *Returns:* `*this`.

```cpp
constexpr month operator++(int) noexcept;
```

**4** *Effects:* `++(*this)`.

**5** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr month& operator--() noexcept;
```

**6** *Effects:* `*this -= months{1}`.

**7** *Returns:* `*this`.

```cpp
constexpr month operator--(int) noexcept;
```

**8** *Effects:* `--(*this)`.

**9** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr month& operator+=(const months& m) noexcept;
```

**10** *Effects:* `*this = *this + m`.

**11** *Returns:* `*this`.

```cpp
constexpr month& operator-=(const months& m) noexcept;
```

**12** *Effects:* `*this = *this - m`.

**13** *Returns:* `*this`.

```cpp
constexpr explicit operator unsigned() const noexcept;
```

**14** *Returns:* `m_`.

```cpp
constexpr bool ok() const noexcept;
```

**15** *Returns:* `1 <= m_ && m_ <= 12`.
