# 27.8.7.2 Member functions [time.cal.wdidx.members]

[Contents](../../README.md) · up: [27.8.7 Class `weekday_indexed` \[time.cal.wdidx\]](time.cal.wdidx.md) · prev: [27.8.7.1 Overview \[time.cal.wdidx.overview\]](time.cal.wdidx.overview.md) · next: [27.8.7.3 Non-member functions \[time.cal.wdidx.nonmembers\]](time.cal.wdidx.nonmembers.md)

```cpp
constexpr weekday_indexed(const chrono::weekday& wd, unsigned index) noexcept;
```

**1** *Effects:* Initializes `wd_` with `wd` and `index_` with `index`. The values held are unspecified if `!wd.ok()` or `index` is not in the range `[0, 7]`.

```cpp
constexpr chrono::weekday weekday() const noexcept;
```

**2** *Returns:* `wd_`.

```cpp
constexpr unsigned index() const noexcept;
```

**3** *Returns:* `index_`.

```cpp
constexpr bool ok() const noexcept;
```

**4** *Returns:* `wd_.ok() && 1 <= index_ && index_ <= 5`.
