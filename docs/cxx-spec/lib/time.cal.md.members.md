# 27.8.9.2 Member functions [time.cal.md.members]

[Contents](../README.md) · up: [27.8.9 Class `month_day` \[time.cal.md\]](time.cal.md.md) · prev: [27.8.9.1 Overview \[time.cal.md.overview\]](time.cal.md.overview.md) · next: [27.8.9.3 Non-member functions \[time.cal.md.nonmembers\]](time.cal.md.nonmembers.md)

```cpp
constexpr month_day(const chrono::month& m, const chrono::day& d) noexcept;
```

**1** *Effects:* Initializes `m_` with `m`, and `d_` with `d`.

```cpp
constexpr chrono::month month() const noexcept;
```

**2** *Returns:* `m_`.

```cpp
constexpr chrono::day day() const noexcept;
```

**3** *Returns:* `d_`.

```cpp
constexpr bool ok() const noexcept;
```

**4** *Returns:* `true` if `m_.ok()` is `true`, `1d <= d_`, and `d_` is less than or equal to the number of days in month `m_`; otherwise returns `false`. When `m_ == February`, the number of days is considered to be 29.
