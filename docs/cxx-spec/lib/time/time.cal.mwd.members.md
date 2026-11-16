# 27.8.11.2 Member functions [time.cal.mwd.members]

[Contents](../../README.md) · up: [27.8.11 Class `month_weekday` \[time.cal.mwd\]](time.cal.mwd.md) · prev: [27.8.11.1 Overview \[time.cal.mwd.overview\]](time.cal.mwd.overview.md) · next: [27.8.11.3 Non-member functions \[time.cal.mwd.nonmembers\]](time.cal.mwd.nonmembers.md)

```cpp
constexpr month_weekday(const chrono::month& m, const chrono::weekday_indexed& wdi) noexcept;
```

**1** *Effects:* Initializes `m_` with `m`, and `wdi_` with `wdi`.

```cpp
constexpr chrono::month month() const noexcept;
```

**2** *Returns:* `m_`.

```cpp
constexpr chrono::weekday_indexed weekday_indexed() const noexcept;
```

**3** *Returns:* `wdi_`.

```cpp
constexpr bool ok() const noexcept;
```

**4** *Returns:* `m_.ok() && wdi_.ok()`.
