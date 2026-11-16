# 27.8.6.2 Member functions [time.cal.wd.members]

[Contents](../../README.md) · up: [27.8.6 Class `weekday` \[time.cal.wd\]](time.cal.wd.md) · prev: [27.8.6.1 Overview \[time.cal.wd.overview\]](time.cal.wd.overview.md) · next: [27.8.6.3 Non-member functions \[time.cal.wd.nonmembers\]](time.cal.wd.nonmembers.md)

```cpp
constexpr explicit weekday(unsigned wd) noexcept;
```

**1** *Effects:* Initializes `wd_` with `wd == 7 ? 0: wd`. The value held is unspecified if `wd` is not in the range `[0, 255]`.

```cpp
constexpr weekday(const sys_days& dp) noexcept;
```

**2** *Effects:* Computes what day of the week corresponds to the `sys_days` `dp`, and initializes that day of the week in `wd_`.

**3** [*Example 1*: If `dp` represents 1970-01-01, the constructed `weekday` represents Thursday by storing `4` in `wd_`. — *end example*]

```cpp
constexpr explicit weekday(const local_days& dp) noexcept;
```

**4** *Effects:* Computes what day of the week corresponds to the `local_days` `dp`, and initializes that day of the week in `wd_`.

**5** *Postconditions:* The value is identical to that constructed from `sys_days{dp.time_since_epoch()}`.

```cpp
constexpr weekday& operator++() noexcept;
```

**6** *Effects:* `*this += days{1}`.

**7** *Returns:* `*this`.

```cpp
constexpr weekday operator++(int) noexcept;
```

**8** *Effects:* `++(*this)`.

**9** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr weekday& operator--() noexcept;
```

**10** *Effects:* `*this -= days{1}`.

**11** *Returns:* `*this`.

```cpp
constexpr weekday operator--(int) noexcept;
```

**12** *Effects:* `--(*this)`.

**13** *Returns:* A copy of `*this` as it existed on entry to this member function.

```cpp
constexpr weekday& operator+=(const days& d) noexcept;
```

**14** *Effects:* `*this = *this + d`.

**15** *Returns:* `*this`.

```cpp
constexpr weekday& operator-=(const days& d) noexcept;
```

**16** *Effects:* `*this = *this - d`.

**17** *Returns:* `*this`.

```cpp
constexpr unsigned c_encoding() const noexcept;
```

**18** *Returns:* `wd_`.

```cpp
constexpr unsigned iso_encoding() const noexcept;
```

**19** *Returns:* `wd_ == 0u ? 7u: wd_`.

```cpp
constexpr bool ok() const noexcept;
```

**20** *Returns:* `wd_ <= 6`.

```cpp
constexpr weekday_indexed operator[](unsigned index) const noexcept;
```

**21** *Returns:* `{*this, index}`.

```cpp
constexpr weekday_last operator[](last_spec) const noexcept;
```

**22** *Returns:* `weekday_last{*this}`.
