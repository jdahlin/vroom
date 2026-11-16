# 27.8.8.2 Member functions [time.cal.wdlast.members]

[Contents](../README.md) · up: [27.8.8 Class `weekday_last` \[time.cal.wdlast\]](time.cal.wdlast.md) · prev: [27.8.8.1 Overview \[time.cal.wdlast.overview\]](time.cal.wdlast.overview.md) · next: [27.8.8.3 Non-member functions \[time.cal.wdlast.nonmembers\]](time.cal.wdlast.nonmembers.md)

```cpp
constexpr explicit weekday_last(const chrono::weekday& wd) noexcept;
```

**1** *Effects:* Initializes `wd_` with `wd`.

```cpp
constexpr chrono::weekday weekday() const noexcept;
```

**2** *Returns:* `wd_`.

```cpp
constexpr bool ok() const noexcept;
```

**3** *Returns:* `wd_.ok()`.
